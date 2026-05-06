/* Path Subsystem Implementation
 * Canonical path classification, normalization, and resolution
 */

#include "path.h"

#include <errno.h>
#include <limits.h>
#include <regex.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fdtable.h"
#include "vfs.h"
#include "../kernel/task.h"

/* ============================================================================
 * PATH CLASSIFICATION
 * ============================================================================ */

static bool path_is_known_host_absolute_impl(const char *path) {
    if (!path || path[0] != '/') {
        return false;
    }

    if (strncmp(path, "/private/", 9) == 0) {
        return true;
    }
    if (strncmp(path, "/var/mobile/", 12) == 0) {
        return true;
    }
    if (strncmp(path, "/Users/", 7) == 0) {
        return true;
    }
    if (strncmp(path, "/Volumes/", 9) == 0) {
        return true;
    }

    return false;
}

path_type_t path_classify(const char *path) {
    if (!path || path[0] == '\0') {
        return PATH_INVALID;
    }

    if (path[0] != '/') {
        return PATH_VIRTUAL_LINUX;
    }

    if (path_is_known_host_absolute_impl(path)) {
        return PATH_ABSOLUTE_HOST;
    }

    if (path_is_external(path)) {
        return PATH_EXTERNAL;
    }

    if (path_is_own_sandbox(path)) {
        return PATH_OWN_SANDBOX;
    }

    if (vfs_path_is_linux_route(path)) {
        return PATH_VIRTUAL_LINUX;
    }

    return PATH_ABSOLUTE_HOST;
}

/* ============================================================================
 * PATH NORMALIZATION
 * ============================================================================ */

void path_normalize(char *path) {
    if (!path || !*path)
        return;

    char *src = path;
    char *dst = path;

    /* Track if path is absolute */
    bool is_absolute = (*src == '/');

    /* Skip leading slashes */
    while (*src == '/') {
        src++;
    }

    /* Handle root case - if absolute, ensure leading slash */
    if (is_absolute) {
        *dst++ = '/';

        /* Handle special case: /./ at start */
        if (src[0] == '.' && (src[1] == '/' || src[1] == '\0')) {
            src++; /* Skip the dot */
            while (*src == '/')
                src++; /* Skip following slashes */
        }
    }

    while (*src) {
        if (*src == '/') {
            /* Skip multiple slashes */
            while (*src == '/')
                src++;

            /* Check for . or .. */
            if (*src == '.') {
                if (src[1] == '\0' || src[1] == '/') {
                    /* Single dot - skip it */
                    src++;
                    continue;
                } else if (src[1] == '.' && (src[2] == '\0' || src[2] == '/')) {
                    /* Double dot - go up one directory */
                    src += 2;
                    if (dst > path + 1) {
                        /* Find previous slash */
                        dst--;
                        while (dst > path && *dst != '/')
                            dst--;
                        if (dst == path)
                            dst++;
                    }
                    continue;
                }
            }

            /* Add single slash */
            if (dst > path && dst[-1] != '/') {
                *dst++ = '/';
            }
        } else {
            *dst++ = *src++;
        }
    }

    /* Remove trailing slash (unless root) */
    if (dst > path + 1 && dst[-1] == '/') {
        dst--;
    }

    *dst = '\0';
}

int path_normalize_with_len(char *path, size_t path_len) {
    (void)path_len;
    if (!path) {
        errno = EINVAL;
        return -1;
    }
    path_normalize(path);
    return 0;
}

/* ============================================================================
 * PATH TRANSLATION (Virtual -> Host)
 * ============================================================================ */

int path_translate(const char *virtual_path, char *host_path, size_t host_path_len) {
    int ret;

    if (!virtual_path || !host_path || host_path_len == 0) {
        errno = EINVAL;
        return -1;
    }

    switch (path_classify(virtual_path)) {
    case PATH_OWN_SANDBOX:
    case PATH_ABSOLUTE_HOST:
        if (strlen(virtual_path) >= host_path_len) {
            errno = ENAMETOOLONG;
            return -1;
        }
        strncpy(host_path, virtual_path, host_path_len - 1);
        host_path[host_path_len - 1] = '\0';
        return 0;

    case PATH_VIRTUAL_LINUX:
        ret = vfs_translate_path(virtual_path, host_path, host_path_len);
        if (ret != 0) {
            errno = -ret;
            return -1;
        }
        return 0;

    default:
        errno = ENOENT;
        return -1;
    }
}

/* ============================================================================
 * REVERSE TRANSLATION (Host -> Virtual)
 * ============================================================================ */

int path_reverse_translate(const char *host_path, char *virtual_path, size_t virtual_path_len) {
    int ret;

    if (!host_path || !virtual_path || virtual_path_len == 0) {
        errno = EINVAL;
        return -1;
    }

    ret = vfs_reverse_translate(host_path, virtual_path, virtual_path_len);
    if (ret != 0) {
        errno = -ret;
        return -1;
    }

    return 0;
}

/* ============================================================================
 * PATH VALIDATION
 * ============================================================================ */

bool path_is_valid(const char *path) {
    if (!path || path[0] == '\0') {
        return false;
    }

    /* Reject paths with null bytes embedded */
    for (const char *p = path; *p; p++) {
        if (*p == '\0') {
            return false;
        }
    }

    /* Reject paths that are too long */
    if (strlen(path) >= PATH_MAX) {
        return false;
    }

    return true;
}

bool path_is_safe(const char *path) {
    if (!path_is_valid(path)) {
        return false;
    }

    /* Reject paths with suspicious patterns */
    if (strstr(path, "..") != NULL) {
        /* Additional validation needed for parent directory escapes */
        return false;
    }

    return true;
}

/* ============================================================================
 * PATH RESOLUTION
 * ============================================================================ */

int path_resolve(const char *path, char *resolved, size_t resolved_len) {
    struct task_struct *task;
    int ret;

    if (!path || !resolved || resolved_len == 0) {
        errno = EINVAL;
        return -1;
    }

    switch (path_classify(path)) {
    case PATH_OWN_SANDBOX:
    case PATH_EXTERNAL:
    case PATH_ABSOLUTE_HOST:
        if (strlen(path) >= resolved_len) {
            errno = ENAMETOOLONG;
            return -1;
        }
        strncpy(resolved, path, resolved_len - 1);
        resolved[resolved_len - 1] = '\0';
        path_normalize(resolved);
        return 0;

    case PATH_VIRTUAL_LINUX:
        task = get_current();
        ret = vfs_translate_path_task(path, resolved, resolved_len, task ? task->fs : NULL);
        if (ret != 0) {
            errno = -ret;
            return -1;
        }
        return 0;

    default:
        errno = ENOENT;
        return -1;
    }
}

void path_join(const char *base, const char *rel, char *result, size_t result_len) {
    if (!base || !rel || !result || result_len == 0) {
        if (result && result_len > 0)
            *result = '\0';
        return;
    }

    if (rel[0] == '/') {
        /* rel is absolute */
        if (strlen(rel) >= result_len) {
            if (result_len > 0)
                *result = '\0';
            return;
        }
        strncpy(result, rel, result_len - 1);
        result[result_len - 1] = '\0';
        return;
    }

    /* Join base and rel */
    size_t base_len = strlen(base);
    size_t rel_len = strlen(rel);

    if (base_len + rel_len + 2 > result_len) {
        if (result_len > 0)
            *result = '\0';
        return;
    }

    if (base_len > 0 && base[base_len - 1] == '/') {
        snprintf(result, result_len, "%s%s", base, rel);
    } else {
        snprintf(result, result_len, "%s/%s", base, rel);
    }
}

bool path_in_sandbox(const char *path) {
    /* For now, allow all paths - will implement sandbox checking later */
    /* TODO: Implement proper sandbox path validation */
    (void)path;
    return true;
}

/* ============================================================================
 * PATH CLASSIFICATION FOR iOS HYBRID ARCHITECTURE
 *
 * Classifies paths into three categories:
 * 1. Virtual Linux paths (/home, /tmp, /etc) - need VFS translation
 * 2. Own app sandbox paths - direct kernel access
 * 3. External paths (security-scoped) - need special handling
 * ============================================================================ */

/* Compiled regex patterns (initialized on first use) */
static regex_t ios_sandbox_regex;
static regex_t ios_simulator_regex;
static regex_t ios_external_regex;
static bool regex_initialized = false;

/* Initialize regex patterns for path detection */
static void path_init_regex_impl(void) {
    extern regex_t ios_sandbox_regex;
    extern regex_t ios_simulator_regex;
    extern regex_t ios_external_regex;
    extern bool regex_initialized;
    if (regex_initialized)
        return;

    /* Device sandbox: /var/mobile/Containers/Data/Application/<UUID>/ */
    regcomp(&ios_sandbox_regex, "^/var/mobile/Containers/Data/Application/[A-Fa-f0-9-]+/",
            REG_EXTENDED | REG_NOSUB);

    /* Simulator sandbox:
     * ~/Library/Developer/CoreSimulator/Devices/<SIM-UUID>/data/Containers/Data/Application/<APP-UUID>/
     */
    regcomp(&ios_simulator_regex,
            "/Library/Developer/CoreSimulator/Devices/[A-Fa-f0-9-]+/data/Containers/Data/"
            "Application/[A-Fa-f0-9-]+/",
            REG_EXTENDED | REG_NOSUB);

    /* External paths: file-provider, iCloud, shared containers */
    regcomp(&ios_external_regex,
            "^(/private/var/mobile/Library/Mobile "
            "Documents/|/private/var/mobile/Containers/Shared/AppGroup/|file-provider://)",
            REG_EXTENDED | REG_NOSUB);

    regex_initialized = true;
}

/* Check if path is a virtual Linux path (needs VFS translation) */
bool path_is_virtual_linux(const char *path) {
    if (!path || !*path)
        return false;

    return path_classify(path) == PATH_VIRTUAL_LINUX;
}

/* Check if path is within own app sandbox */
bool path_is_own_sandbox(const char *path) {
    if (!path || !*path)
        return false;

    path_init_regex_impl();

    /* Check device sandbox pattern */
    if (regexec(&ios_sandbox_regex, path, 0, NULL, 0) == 0) {
        return true;
    }

    /* Check simulator sandbox pattern */
    if (regexec(&ios_simulator_regex, path, 0, NULL, 0) == 0) {
        return true;
    }

    /* Also check common writable locations that are always accessible */
    /* These are subdirectories that might be passed without full sandbox path */
    if (strstr(path, "/Library/") || strstr(path, "/Library")) {
        return true;
    }

    return false;
}

/* Check if path is external (requires security-scoped access) */
bool path_is_external(const char *path) {
    if (!path || !*path)
        return false;

    path_init_regex_impl();

    /* Check external path patterns */
    if (regexec(&ios_external_regex, path, 0, NULL, 0) == 0) {
        return true;
    }

    /* Check for iCloud Drive patterns */
    if (strstr(path, "/Mobile Documents/")) {
        return true;
    }

    /* Check for shared app groups */
    if (strstr(path, "/Containers/Shared/AppGroup/")) {
        return true;
    }

    /* If it's not a Linux-visible route and not own sandbox, treat as external */
    if (!vfs_path_is_linux_route(path) && !path_is_own_sandbox(path)) {
        /* This is a catch-all for paths we can't categorize */
        /* The iOS kernel will ultimately enforce permissions */
        return true;
    }

    return false;
}

/* Convert virtual Linux path to iOS path using VFS */
int path_virtual_to_ios(const char *vpath, char *ios_path, size_t ios_path_len) {
    if (!vpath || !ios_path || ios_path_len == 0) {
        return -1;
    }

    /* Check if it's actually a virtual path */
    if (!path_is_virtual_linux(vpath)) {
        /* Not a virtual path, copy as-is */
        if (strlen(vpath) >= ios_path_len) {
            errno = ENAMETOOLONG;
            return -1;
        }
        strncpy(ios_path, vpath, ios_path_len - 1);
        ios_path[ios_path_len - 1] = '\0';
        return 0;
    }

    /* Use existing VFS resolution */
    /* This function will be called from VFS layer */
    /* For now, return the path as-is and let VFS handle it */
    if (strlen(vpath) >= ios_path_len) {
        errno = ENAMETOOLONG;
        return -1;
    }
    strncpy(ios_path, vpath, ios_path_len - 1);
    ios_path[ios_path_len - 1] = '\0';

    return 0;
}

/* Check if path can be accessed directly (not virtual) */
bool path_is_direct(const char *path) {
    if (!path || !*path)
        return false;

    path_type_t type = path_classify(path);
    return (type == PATH_OWN_SANDBOX || type == PATH_EXTERNAL);
}

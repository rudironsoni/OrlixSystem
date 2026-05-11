/* Path Subsystem Implementation
 * Canonical path classification, normalization, and resolution
 */

#include "path.h"

#include <linux/errno.h>
#include <linux/string.h>

#include "fdtable.h"
#include "internal/fs/namei.h"
#include "vfs.h"
#include "../kernel/task.h"

/* ============================================================================
 * PATH CLASSIFICATION
 * ============================================================================ */

static bool path_is_known_backing_absolute_impl(const char *path) {
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

    if (path_is_known_backing_absolute_impl(path)) {
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
        return -EINVAL;
    }
    path_normalize(path);
    return 0;
}

/* ============================================================================
 * PATH TRANSLATION (Virtual -> Host)
 * ============================================================================ */

int path_translate(const char *virtual_path, char *backing_path, size_t backing_path_len) {
    int ret;

    if (!virtual_path || !backing_path || backing_path_len == 0) {
        return -EINVAL;
    }

    switch (path_classify(virtual_path)) {
    case PATH_OWN_SANDBOX:
    case PATH_ABSOLUTE_HOST:
        if (strlen(virtual_path) >= backing_path_len) {
            return -ENAMETOOLONG;
        }
        strncpy(backing_path, virtual_path, backing_path_len - 1);
        backing_path[backing_path_len - 1] = '\0';
        return 0;

    case PATH_VIRTUAL_LINUX:
        ret = vfs_translate_path(virtual_path, backing_path, backing_path_len);
        if (ret != 0) {
            return ret;
        }
        return 0;

    default:
        return -ENOENT;
    }
}

/* ============================================================================
 * REVERSE TRANSLATION (Host -> Virtual)
 * ============================================================================ */

int path_reverse_translate(const char *backing_path, char *virtual_path, size_t virtual_path_len) {
    int ret;

    if (!backing_path || !virtual_path || virtual_path_len == 0) {
        return -EINVAL;
    }

    ret = vfs_reverse_translate(backing_path, virtual_path, virtual_path_len);
    if (ret != 0) {
        return ret;
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
    if (strlen(path) >= MAX_PATH) {
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
        return -EINVAL;
    }

    switch (path_classify(path)) {
    case PATH_OWN_SANDBOX:
    case PATH_EXTERNAL:
    case PATH_ABSOLUTE_HOST:
        if (strlen(path) >= resolved_len) {
            return -ENAMETOOLONG;
        }
        strncpy(resolved, path, resolved_len - 1);
        resolved[resolved_len - 1] = '\0';
        path_normalize(resolved);
        return 0;

    case PATH_VIRTUAL_LINUX:
        task = get_current();
        ret = vfs_translate_path_task(path, resolved, resolved_len, task ? task->fs : NULL);
        if (ret != 0) {
            return ret;
        }
        return 0;

    default:
        return -ENOENT;
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

    memcpy(result, base, base_len);
    if (base_len > 0 && base[base_len - 1] == '/') {
        memcpy(result + base_len, rel, rel_len);
        result[base_len + rel_len] = '\0';
    } else {
        result[base_len] = '/';
        memcpy(result + base_len + 1, rel, rel_len);
        result[base_len + rel_len + 1] = '\0';
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
    return backing_path_is_own_sandbox(path);
}

/* Check if path is external (requires security-scoped access) */
bool path_is_external(const char *path) {
    if (!path || !*path)
        return false;
    return backing_path_is_external(path);
}

/* Convert virtual Linux path to iOS path using VFS */
int path_virtual_to_ios(const char *vpath, char *ios_path, size_t ios_path_len) {
    if (!vpath || !ios_path || ios_path_len == 0) {
        return -EINVAL;
    }

    /* Check if it's actually a virtual path */
    if (!path_is_virtual_linux(vpath)) {
        /* Not a virtual path, copy as-is */
        if (strlen(vpath) >= ios_path_len) {
            return -ENAMETOOLONG;
        }
        strncpy(ios_path, vpath, ios_path_len - 1);
        ios_path[ios_path_len - 1] = '\0';
        return 0;
    }

    /* Use existing VFS resolution */
    /* This function will be called from VFS layer */
    /* For now, return the path as-is and let VFS handle it */
    if (strlen(vpath) >= ios_path_len) {
        return -ENAMETOOLONG;
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

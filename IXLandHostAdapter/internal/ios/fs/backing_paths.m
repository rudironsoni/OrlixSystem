/* IXLandSystem/internal/ios/fs/backing_paths.m
 * iOS container path resolution - quarantined in private bridge
 *
 * This file uses Foundation APIs to query iOS container paths.
 * It is the only file in the VFS subsystem that includes Darwin/Foundation headers.
 */

#import <Foundation/Foundation.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>

#define MAX_PATH 4096

/* Forward declarations for C interop */
int host_get_application_support_path_impl(char *path, size_t path_len);
int host_get_caches_path_impl(char *path, size_t path_len);
int host_get_tmp_path_impl(char *path, size_t path_len);
int host_ensure_directory_impl(const char *path, mode_t mode);

int host_get_application_support_path_impl(char *path, size_t path_len) {
    @autoreleasepool {
        NSURL *url = [[NSFileManager defaultManager]
                      URLForDirectory:NSApplicationSupportDirectory
                      inDomain:NSUserDomainMask
                      appropriateForURL:nil
                      create:YES
                      error:nil];
        if (!url) {
            errno = ENOENT;
            return -1;
        }
        const char *cpath = [url fileSystemRepresentation];
        if (!cpath || strlen(cpath) >= path_len) {
            errno = ENAMETOOLONG;
            return -1;
        }
        strncpy(path, cpath, path_len - 1);
        path[path_len - 1] = '\0';
        return 0;
    }
}

int host_get_caches_path_impl(char *path, size_t path_len) {
    @autoreleasepool {
        NSURL *url = [[NSFileManager defaultManager]
                      URLForDirectory:NSCachesDirectory
                      inDomain:NSUserDomainMask
                      appropriateForURL:nil
                      create:YES
                      error:nil];
        if (!url) {
            errno = ENOENT;
            return -1;
        }
        const char *cpath = [url fileSystemRepresentation];
        if (!cpath || strlen(cpath) >= path_len) {
            errno = ENAMETOOLONG;
            return -1;
        }
        strncpy(path, cpath, path_len - 1);
        path[path_len - 1] = '\0';
        return 0;
    }
}

int host_get_tmp_path_impl(char *path, size_t path_len) {
    @autoreleasepool {
        NSString *tmp = NSTemporaryDirectory();
        if (!tmp) {
            errno = ENOENT;
            return -1;
        }
        const char *cpath = [tmp UTF8String];
        if (!cpath || strlen(cpath) >= path_len) {
            errno = ENAMETOOLONG;
            return -1;
        }
        strncpy(path, cpath, path_len - 1);
        path[path_len - 1] = '\0';
        return 0;
    }
}

static int ensure_dir_recursive(const char *path, mode_t mode) {
    struct stat st;
    if (stat(path, &st) == 0) {
        if (S_ISDIR(st.st_mode)) {
            return 0;
        }
        errno = ENOTDIR;
        return -1;
    }
    if (errno != ENOENT) {
        return -1;
    }

    /* Create parent directories recursively */
    char parent[MAX_PATH];
    size_t len = strlen(path);
    if (len >= sizeof(parent)) {
        errno = ENAMETOOLONG;
        return -1;
    }
    memcpy(parent, path, len + 1);

    /* Find the last slash */
    char *last_slash = strrchr(parent, '/');
    if (last_slash && last_slash != parent) {
        *last_slash = '\0';
        if (ensure_dir_recursive(parent, mode) < 0) {
            return -1;
        }
    }

    /* Create this directory */
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    int ret = syscall(SYS_mkdir, path, mode);
#pragma clang diagnostic pop
    if (ret < 0 && errno != EEXIST) {
        return -1;
    }
    return 0;
}

int host_ensure_directory_impl(const char *path, mode_t mode) {
    return ensure_dir_recursive(path, mode);
}

/* VFS root discovery - thin wrappers around existing host path discovery */
int vfs_discover_persistent_root(char *path, size_t path_len) {
    return host_get_application_support_path_impl(path, path_len);
}

int vfs_discover_cache_root(char *path, size_t path_len) {
    return host_get_caches_path_impl(path, path_len);
}

int vfs_discover_temp_root(char *path, size_t path_len) {
    return host_get_tmp_path_impl(path, path_len);
}

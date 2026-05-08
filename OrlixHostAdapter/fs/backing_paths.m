/* OrlixHostAdapter/fs/backing_paths.m
 * iOS container path resolution for backing roots
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
static const char kOrlixStorageLeaf[] = "Orlix";

/* Forward declarations for C interop */
int application_support_path(char *path, size_t path_len);
int caches_path(char *path, size_t path_len);
int temporary_path(char *path, size_t path_len);
int backing_ensure_directory(const char *path, uint32_t mode);

int application_support_path(char *path, size_t path_len) {
    @autoreleasepool {
        NSFileManager *manager = [NSFileManager defaultManager];
        NSURL *baseURL = [manager URLForDirectory:NSApplicationSupportDirectory
                                         inDomain:NSUserDomainMask
                                appropriateForURL:nil
                                           create:YES
                                            error:nil];
        NSURL *url;
        if (!baseURL) {
            errno = ENOENT;
            return -1;
        }
        url = [baseURL URLByAppendingPathComponent:@(kOrlixStorageLeaf) isDirectory:YES];
        if (![manager createDirectoryAtURL:url
               withIntermediateDirectories:YES
                                attributes:nil
                                     error:nil]) {
            errno = EIO;
            return -1;
        }
        const char *cpath = [url fileSystemRepresentation];
        size_t len;
        if (!cpath) {
            errno = ENOENT;
            return -1;
        }
        len = strlen(cpath);
        if (len >= path_len) {
            errno = ENAMETOOLONG;
            return -1;
        }
        memcpy(path, cpath, len + 1);
        return 0;
    }
}

int caches_path(char *path, size_t path_len) {
    @autoreleasepool {
        NSFileManager *manager = [NSFileManager defaultManager];
        NSURL *baseURL = [manager URLForDirectory:NSCachesDirectory
                                         inDomain:NSUserDomainMask
                                appropriateForURL:nil
                                           create:YES
                                            error:nil];
        NSURL *url;
        if (!baseURL) {
            errno = ENOENT;
            return -1;
        }
        url = [baseURL URLByAppendingPathComponent:@(kOrlixStorageLeaf) isDirectory:YES];
        if (![manager createDirectoryAtURL:url
               withIntermediateDirectories:YES
                                attributes:nil
                                     error:nil]) {
            errno = EIO;
            return -1;
        }
        const char *cpath = [url fileSystemRepresentation];
        size_t len;
        if (!cpath) {
            errno = ENOENT;
            return -1;
        }
        len = strlen(cpath);
        if (len >= path_len) {
            errno = ENAMETOOLONG;
            return -1;
        }
        memcpy(path, cpath, len + 1);
        return 0;
    }
}

int temporary_path(char *path, size_t path_len) {
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

static int ensure_dir_recursive(const char *path, uint32_t mode) {
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
    int ret = syscall(SYS_mkdir, path, (mode_t)mode);
#pragma clang diagnostic pop
    if (ret < 0 && errno != EEXIST) {
        return -1;
    }
    return 0;
}

int backing_ensure_directory(const char *path, uint32_t mode) {
    return ensure_dir_recursive(path, mode);
}

/* VFS root discovery - thin wrappers around backing path discovery. */
int backing_root_discover_persistent(char *path, size_t path_len) {
    return application_support_path(path, path_len);
}

int backing_root_discover_cache(char *path, size_t path_len) {
    return caches_path(path, path_len);
}

int backing_root_discover_temp(char *path, size_t path_len) {
    return temporary_path(path, path_len);
}

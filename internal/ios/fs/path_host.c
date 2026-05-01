/* internal/ios/fs/path_host.c
 * Host path operations bridge implementation
 *
 * This file contains the host-specific implementations for path operations.
 * All Darwin host calls are isolated here, providing a narrow seam for
 * Linux-owner code in fs/namei.c
 */

/* Include shared stat type definition */
#include "fs/stat_types.h"

#include "path_host.h"

/* Darwin headers - these define S_IFMT, S_ISDIR, etc. which are compatible
 * with Linux ABI values, so we use them directly in bridge code */
#include <sys/stat.h>
#include <sys/stdio.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <dirent.h>
#include <string.h>

#include "errno_host.h"

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"


/* Translate Darwin struct stat to Linux struct linux_stat */
static void translate_stat_to_linux(const struct stat *darwin_stat, struct linux_stat *linux_stat)
{
    memset(linux_stat, 0, sizeof(*linux_stat));
    linux_stat->st_dev = darwin_stat->st_dev;
    linux_stat->st_ino = darwin_stat->st_ino;
    linux_stat->st_mode = darwin_stat->st_mode;
    linux_stat->st_nlink = darwin_stat->st_nlink;
    linux_stat->st_uid = darwin_stat->st_uid;
    linux_stat->st_gid = darwin_stat->st_gid;
    linux_stat->st_rdev = darwin_stat->st_rdev;
    linux_stat->st_size = darwin_stat->st_size;
    linux_stat->st_blksize = darwin_stat->st_blksize;
    linux_stat->st_blocks = darwin_stat->st_blocks;
    /* Darwin has timespec fields for timestamps */
    linux_stat->st_atime_sec = darwin_stat->st_atimespec.tv_sec;
    linux_stat->st_atime_nsec = (unsigned long long)darwin_stat->st_atimespec.tv_nsec;
    linux_stat->st_mtime_sec = darwin_stat->st_mtimespec.tv_sec;
    linux_stat->st_mtime_nsec = (unsigned long long)darwin_stat->st_mtimespec.tv_nsec;
    linux_stat->st_ctime_sec = darwin_stat->st_ctimespec.tv_sec;
    linux_stat->st_ctime_nsec = (unsigned long long)darwin_stat->st_ctimespec.tv_nsec;
}

/* Host stat operations - return Linux-shaped negative errno on failure */
int host_stat_impl(const char *path, struct linux_stat *statbuf)
{
    struct stat darwin_stat;
    int ret = (int)syscall(SYS_stat64, path, &darwin_stat);
    if (ret == 0) {
        translate_stat_to_linux(&darwin_stat, statbuf);
        return 0;
    }
    return -host_errno_to_linux_errno(errno);
}

int host_lstat_impl(const char *path, struct linux_stat *statbuf)
{
    struct stat darwin_stat;
    int ret = (int)syscall(SYS_lstat64, path, &darwin_stat);
    if (ret == 0) {
        translate_stat_to_linux(&darwin_stat, statbuf);
        return 0;
    }
    return -host_errno_to_linux_errno(errno);
}

int host_access_impl(const char *path, int mode)
{
    int ret = (int)syscall(SYS_access, path, mode);
    if (ret == 0) {
        return 0;
    }
    return -host_errno_to_linux_errno(errno);
}

int host_directory_is_empty_impl(const char *path)
{
    DIR *dir = opendir(path);
    struct dirent *entry;

    if (!dir) {
        return -host_errno_to_linux_errno(errno);
    }

    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
            closedir(dir);
            return 0;
        }
    }

    closedir(dir);
    return 1;
}

/* Host rename operation (Darwin renameatx_np) */
int host_renameatx_np_impl(int fromfd, const char *from, int tofd, const char *to, unsigned int flags)
{
    return renameatx_np(fromfd, from, tofd, to, flags);
}

int host_rename_exchange_impl(const char *from, const char *to)
{
    return renameatx_np(AT_FDCWD, from, AT_FDCWD, to, RENAME_SWAP);
}

/* Directory operations */
int host_mkdir_impl(const char *pathname, uint32_t mode)
{
    return (int)syscall(SYS_mkdir, pathname, (mode_t)mode);
}

int host_rmdir_impl(const char *pathname)
{
    return (int)syscall(SYS_rmdir, pathname);
}

/* File operations */
int host_unlink_impl(const char *pathname)
{
    return (int)syscall(SYS_unlink, pathname);
}

int host_link_impl(const char *oldpath, const char *newpath)
{
    return (int)syscall(SYS_link, oldpath, newpath);
}

int host_linkat_impl(const char *oldpath, const char *newpath, int follow_symlink)
{
    return (int)syscall(SYS_linkat, AT_FDCWD, oldpath, AT_FDCWD, newpath,
                        follow_symlink ? AT_SYMLINK_FOLLOW : 0);
}

int host_symlink_impl(const char *target, const char *linkpath)
{
    return (int)syscall(SYS_symlink, target, linkpath);
}

ssize_t host_readlink_impl(const char *pathname, char *buf, size_t bufsiz)
{
    return (ssize_t)syscall(SYS_readlink, pathname, buf, bufsiz);
}

/* Fchdir */
int host_fchdir_impl(int fd)
{
    return (int)syscall(SYS_fchdir, fd);
}

#pragma clang diagnostic pop

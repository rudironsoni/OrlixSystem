/* OrlixHostAdapter/fs/path.c
 * Backing path operations implementation
 *
 * This file contains the host-specific implementations for path operations.
 * All Darwin host calls are isolated here, providing a narrow seam for
 * Linux-owner code in fs/namei.c
 */

#include "backing_stat_translate.h"

/* Darwin headers - these define S_IFMT, S_ISDIR, etc. which are compatible
 * with Linux ABI values, so we use them directly in bridge code */
#include <sys/stat.h>
#include <sys/stdio.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <errno.h>
#include <dirent.h>
#include <regex.h>
#include <stdbool.h>
#include <string.h>

#include "backing_io_internal.h"
#include "errno_translation.h"

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"

static regex_t ios_sandbox_regex;
static regex_t ios_simulator_regex;
static regex_t ios_external_regex;
static bool regex_initialized = false;

enum {
    host_linux_at_fdcwd = -100,
    host_linux_at_symlink_follow = 0x400,
    host_linux_at_rename_noreplace = 0x0001,
    host_linux_at_rename_exchange = 0x0002,
};

static void backing_path_init_regex(void) {
    if (regex_initialized) {
        return;
    }

    regcomp(&ios_sandbox_regex, "^/var/mobile/Containers/Data/Application/[A-Fa-f0-9-]+/",
            REG_EXTENDED | REG_NOSUB);
    regcomp(&ios_simulator_regex,
            "/Library/Developer/CoreSimulator/Devices/[A-Fa-f0-9-]+/data/Containers/Data/"
            "Application/[A-Fa-f0-9-]+/",
            REG_EXTENDED | REG_NOSUB);
    regcomp(&ios_external_regex,
            "^(/private/var/mobile/Library/Mobile "
            "Documents/|/private/var/mobile/Containers/Shared/AppGroup/|file-provider://)",
            REG_EXTENDED | REG_NOSUB);

    regex_initialized = true;
}


static void capture_backing_stat(const struct stat *source, struct backing_stat_data *target) {
    target->dev = source->st_dev;
    target->ino = source->st_ino;
    target->mode = source->st_mode;
    target->nlink = source->st_nlink;
    target->uid = source->st_uid;
    target->gid = source->st_gid;
    target->rdev = source->st_rdev;
    target->size = source->st_size;
    target->blksize = source->st_blksize;
    target->blocks = source->st_blocks;
    target->atime_sec = source->st_atimespec.tv_sec;
    target->atime_nsec = (uint64_t)source->st_atimespec.tv_nsec;
    target->mtime_sec = source->st_mtimespec.tv_sec;
    target->mtime_nsec = (uint64_t)source->st_mtimespec.tv_nsec;
    target->ctime_sec = source->st_ctimespec.tv_sec;
    target->ctime_nsec = (uint64_t)source->st_ctimespec.tv_nsec;
}

/* Backing stat operations return Linux-shaped negative errno on failure. */
int backing_stat(const char *path, struct stat *statbuf)
{
    struct stat darwin_stat;
    struct backing_stat_data data;
    int ret = (int)syscall(SYS_stat64, path, &darwin_stat);
    if (ret == 0) {
        capture_backing_stat(&darwin_stat, &data);
        backing_stat_translate(&data, statbuf);
        return 0;
    }
    return -linux_errno_from_darwin_errno(errno);
}

int backing_lstat(const char *path, struct stat *statbuf)
{
    struct stat darwin_stat;
    struct backing_stat_data data;
    int ret = (int)syscall(SYS_lstat64, path, &darwin_stat);
    if (ret == 0) {
        capture_backing_stat(&darwin_stat, &data);
        backing_stat_translate(&data, statbuf);
        return 0;
    }
    return -linux_errno_from_darwin_errno(errno);
}

int backing_access(const char *path, int mode)
{
    int ret = (int)syscall(SYS_access, path, mode);
    if (ret == 0) {
        return 0;
    }
    return -linux_errno_from_darwin_errno(errno);
}

bool backing_path_is_own_sandbox(const char *path)
{
    if (!path || !*path) {
        return false;
    }

    backing_path_init_regex();
    if (regexec(&ios_sandbox_regex, path, 0, NULL, 0) == 0) {
        return true;
    }
    if (regexec(&ios_simulator_regex, path, 0, NULL, 0) == 0) {
        return true;
    }
    if (strstr(path, "/Library/") || strstr(path, "/Library")) {
        return true;
    }
    return false;
}

bool backing_path_is_external(const char *path)
{
    if (!path || !*path) {
        return false;
    }

    backing_path_init_regex();
    if (regexec(&ios_external_regex, path, 0, NULL, 0) == 0) {
        return true;
    }
    if (strstr(path, "/Mobile Documents/")) {
        return true;
    }
    if (strstr(path, "/Containers/Shared/AppGroup/")) {
        return true;
    }
    return false;
}

int backing_directory_is_empty(const char *path)
{
    DIR *dir = opendir(path);
    struct dirent *entry;

    if (!dir) {
        return -linux_errno_from_darwin_errno(errno);
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
int backing_rename_with_flags(int fromfd, const char *from, int tofd, const char *to, unsigned int flags)
{
    unsigned int host_flags = 0;
    if ((flags & host_linux_at_rename_noreplace) != 0) {
        host_flags |= RENAME_EXCL;
    }
    if ((flags & host_linux_at_rename_exchange) != 0) {
        host_flags |= RENAME_SWAP;
    }
    int ret = renameatx_np(fromfd, from, tofd, to, host_flags);
    if (ret == 0) {
        return 0;
    }
    return -linux_errno_from_darwin_errno(errno);
}

int backing_rename_exchange(const char *from, const char *to)
{
    int ret = renameatx_np(host_linux_at_fdcwd, from, host_linux_at_fdcwd, to, RENAME_SWAP);
    if (ret == 0) {
        return 0;
    }
    return -linux_errno_from_darwin_errno(errno);
}

/* Directory operations */
int backing_mkdir(const char *pathname, uint32_t mode)
{
    int ret = (int)syscall(SYS_mkdir, pathname, (mode_t)mode);
    if (ret == 0) {
        return 0;
    }
    return -linux_errno_from_darwin_errno(errno);
}

int backing_rmdir(const char *pathname)
{
    int ret = (int)syscall(SYS_rmdir, pathname);
    if (ret == 0) {
        return 0;
    }
    return -linux_errno_from_darwin_errno(errno);
}

/* File operations */
int backing_unlink(const char *pathname)
{
    int ret = (int)syscall(SYS_unlink, pathname);
    if (ret == 0) {
        return 0;
    }
    return -linux_errno_from_darwin_errno(errno);
}

int backing_link(const char *oldpath, const char *newpath)
{
    int ret = (int)syscall(SYS_link, oldpath, newpath);
    if (ret == 0) {
        return 0;
    }
    return -linux_errno_from_darwin_errno(errno);
}

int backing_linkat(const char *oldpath, const char *newpath, int follow_symlink)
{
    int ret = (int)syscall(SYS_linkat, host_linux_at_fdcwd, oldpath, host_linux_at_fdcwd, newpath,
                           follow_symlink ? host_linux_at_symlink_follow : 0);
    if (ret == 0) {
        return 0;
    }
    return -linux_errno_from_darwin_errno(errno);
}

int backing_symlink(const char *target, const char *linkpath)
{
    int ret = (int)syscall(SYS_symlink, target, linkpath);
    if (ret == 0) {
        return 0;
    }
    return -linux_errno_from_darwin_errno(errno);
}

long backing_readlink(const char *pathname, char *buf, size_t bufsiz)
{
    long ret = (long)syscall(SYS_readlink, pathname, buf, bufsiz);
    if (ret >= 0) {
        return ret;
    }
    return -linux_errno_from_darwin_errno(errno);
}

/* Fchdir */
int backing_fchdir(int fd)
{
    int ret = (int)syscall(SYS_fchdir, fd);
    if (ret == 0) {
        return 0;
    }
    return -linux_errno_from_darwin_errno(errno);
}

#pragma clang diagnostic pop

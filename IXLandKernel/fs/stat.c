/* IXLandKernel/fs/stat.c
 * Virtual stat/fstat implementation
 */
#include <linux/fcntl.h>
#include <linux/stat.h>

#include <errno.h>
#include <string.h>

#include "vfs.h"
#include "fdtable.h"
#include "fs_sync.h"
#include "internal/private/backing_io.h"
#include "internal/private/backing_path.h"
#include "vfs.h"

#ifndef MAX_PATH
#define MAX_PATH 4096
#endif

int stat_impl(const char *pathname, struct linux_stat *statbuf) {
    int ret;

    if (!pathname || !statbuf) {
        errno = EFAULT;
        return -1;
    }

    if (vfs_path_is_linux_route(pathname)) {
        ret = vfs_fstatat(AT_FDCWD, pathname, statbuf, 0);
        if (ret != 0) {
            errno = -ret;
            return -1;
        }
        return 0;
    }

    ret = backing_stat(pathname, statbuf);
    if (ret != 0) {
        errno = -ret;
        return -1;
    }
    return 0;
}

int fstat_impl(int fd, struct linux_stat *statbuf) {
    int ret;
    int real_fd;
    char path[MAX_PATH];
    void *entry;

    if (!statbuf) {
        errno = EFAULT;
        return -1;
    }

    if (fd < 0 || fd >= NR_OPEN_DEFAULT) {
        errno = EBADF;
        return -1;
    }

    entry = get_fd_entry_impl(fd);
    if (!entry) {
        errno = EBADF;
        return -1;
    }

    if (get_fd_is_pipe_impl(entry)) {
        memset(statbuf, 0, sizeof(*statbuf));
        statbuf->st_mode = S_IFIFO | 0600;
        statbuf->st_nlink = 1;
        statbuf->st_uid = 0;
        statbuf->st_gid = 0;
        statbuf->st_blksize = 4096;
        statbuf->st_blocks = 0;
        put_fd_entry_impl(entry);
        return 0;
    }

    real_fd = get_real_fd_impl(entry);
    if (real_fd >= 0) {
        ret = backing_fstat(real_fd, statbuf);
        if (ret == 0 && get_fd_is_memfd_impl(entry)) {
            statbuf->st_mode = S_IFREG | (statbuf->st_mode & 0777U);
        }
        if (ret == 0 && get_fd_path_impl(entry, path, sizeof(path)) == 0) {
            vfs_apply_stat_metadata(path, statbuf);
        }
        put_fd_entry_impl(entry);
        if (ret != 0) {
            errno = -ret;
            return -1;
        }
        return 0;
    }

    ret = get_fd_path_impl(entry, path, sizeof(path));
    put_fd_entry_impl(entry);
    if (ret != 0) {
        return -1;
    }

    ret = vfs_fstatat(AT_FDCWD, path, statbuf, 0);
    if (ret != 0) {
        errno = -ret;
        return -1;
    }
    return 0;
}

int lstat_impl(const char *pathname, struct linux_stat *statbuf) {
    int ret;

    if (!pathname || !statbuf) {
        errno = EFAULT;
        return -1;
    }

    if (vfs_path_is_linux_route(pathname)) {
        ret = vfs_fstatat(AT_FDCWD, pathname, statbuf, AT_SYMLINK_NOFOLLOW);
        if (ret != 0) {
            errno = -ret;
            return -1;
        }
        return 0;
    }

    ret = backing_lstat(pathname, statbuf);
    if (ret != 0) {
        errno = -ret;
        return -1;
    }
    return 0;
}

int access_impl(const char *pathname, int mode) {
    int ret;

    if (!pathname) {
        errno = EFAULT;
        return -1;
    }

    ret = vfs_access(pathname, mode);
    if (ret != 0) {
        errno = -ret;
        return -1;
    }
    return 0;
}

int fstatat_impl(int dirfd, const char *pathname, struct linux_stat *statbuf, int flags) {
    int ret;

    if (!pathname || !statbuf) {
        errno = EFAULT;
        return -1;
    }

    ret = vfs_fstatat(dirfd, pathname, statbuf, flags);
    if (ret != 0) {
        errno = -ret;
        return -1;
    }
    return 0;
}

int faccessat_impl(int dirfd, const char *pathname, int mode, int flags) {
    int ret;

    if (!pathname) {
        errno = EFAULT;
        return -1;
    }

    ret = vfs_faccessat(dirfd, pathname, mode, flags);
    if (ret != 0) {
        errno = -ret;
        return -1;
    }
    return 0;
}

static void statx_timestamp_from_linux_stat(struct statx_timestamp *dst, long sec,
                                            unsigned long nsec) {
    if (!dst) {
        return;
    }
    dst->tv_sec = sec;
    dst->tv_nsec = (__u32)nsec;
    dst->__reserved = 0;
}

static void statx_from_linux_stat(struct statx *dst, const struct linux_stat *src,
                                  unsigned int mask) {
    memset(dst, 0, sizeof(*dst));
    dst->stx_mask = STATX_BASIC_STATS & ~STATX_BTIME;
    if ((mask & STATX_MNT_ID) != 0) {
        dst->stx_mask |= STATX_MNT_ID;
    }
    dst->stx_blksize = (__u32)src->st_blksize;
    dst->stx_nlink = src->st_nlink;
    dst->stx_uid = src->st_uid;
    dst->stx_gid = src->st_gid;
    dst->stx_mode = (__u16)src->st_mode;
    dst->stx_ino = src->st_ino;
    dst->stx_size = src->st_size;
    dst->stx_blocks = src->st_blocks;
    statx_timestamp_from_linux_stat(&dst->stx_atime, src->st_atime_sec, src->st_atime_nsec);
    statx_timestamp_from_linux_stat(&dst->stx_mtime, src->st_mtime_sec, src->st_mtime_nsec);
    statx_timestamp_from_linux_stat(&dst->stx_ctime, src->st_ctime_sec, src->st_ctime_nsec);
    dst->stx_rdev_major = (__u32)((src->st_rdev >> 8) & 0xfffU);
    dst->stx_rdev_minor = (__u32)((src->st_rdev & 0xffU) | ((src->st_rdev >> 12) & 0xfffff00U));
    dst->stx_dev_major = (__u32)((src->st_dev >> 8) & 0xfffU);
    dst->stx_dev_minor = (__u32)((src->st_dev & 0xffU) | ((src->st_dev >> 12) & 0xfffff00U));
    if ((mask & STATX_MNT_ID) != 0) {
        dst->stx_mnt_id = 1;
    }
}

int statx_impl(int dirfd, const char *pathname, int flags, unsigned int mask,
               struct statx *statxbuf) {
    struct linux_stat st;
    int stat_flags = flags & AT_SYMLINK_NOFOLLOW;
    int ret;

    if (!pathname || !statxbuf) {
        errno = EFAULT;
        return -1;
    }
    if ((mask & STATX__RESERVED) != 0 ||
        (flags & ~(AT_EMPTY_PATH | AT_SYMLINK_NOFOLLOW | AT_NO_AUTOMOUNT |
                   AT_STATX_SYNC_TYPE)) != 0) {
        errno = EINVAL;
        return -1;
    }
    if ((flags & AT_EMPTY_PATH) != 0 && pathname[0] == '\0') {
        ret = fstat_impl(dirfd, &st);
    } else {
        ret = fstatat_impl(dirfd, pathname, &st, stat_flags);
    }
    if (ret != 0) {
        return -1;
    }
    statx_from_linux_stat(statxbuf, &st, mask);
    return 0;
}

__attribute__((visibility("default"))) int stat(const char *pathname, struct linux_stat *statbuf) {
    return stat_impl(pathname, statbuf);
}

__attribute__((visibility("default"))) int fstat(int fd, struct linux_stat *statbuf) {
    return fstat_impl(fd, statbuf);
}

__attribute__((visibility("default"))) int lstat(const char *pathname, struct linux_stat *statbuf) {
    return lstat_impl(pathname, statbuf);
}

__attribute__((visibility("default"))) int access(const char *pathname, int mode) {
    return access_impl(pathname, mode);
}

__attribute__((visibility("default"))) int faccessat(int dirfd, const char *pathname, int mode, int flags) {
    return faccessat_impl(dirfd, pathname, mode, flags);
}

__attribute__((visibility("default"))) int fstatat(int dirfd, const char *pathname, struct linux_stat *statbuf, int flags) {
    return fstatat_impl(dirfd, pathname, statbuf, flags);
}

__attribute__((visibility("default"))) int newfstatat(int dirfd, const char *pathname, struct linux_stat *statbuf, int flags) {
    return fstatat_impl(dirfd, pathname, statbuf, flags);
}

__attribute__((visibility("default"))) int statx(int dirfd, const char *pathname, int flags,
                                                  unsigned int mask, struct statx *statxbuf) {
    return statx_impl(dirfd, pathname, flags, mask, statxbuf);
}

#include "backing_io.h"

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "errno_host.h"
#include "fs/stat_types.h"

extern int host_translate_open_flags_impl(int flags,
                                          int host_rdonly,
                                          int host_wronly,
                                          int host_rdwr,
                                          int host_creat,
                                          int host_excl,
                                          int host_trunc,
                                          int host_append,
                                          int host_nonblock,
                                          int host_directory,
                                          int host_nofollow);

/* Private host mediation via direct syscalls
 *
 * This file provides non-interposing access to host Darwin syscalls.
 * We use the syscall() interface with SYS_* constants to call host
 * operations without recursion through IXLand's exported wrappers.
 *
 * Note: syscall() is deprecated on Darwin but remains functional.
 * This is acceptable for a virtualization layer that needs to bypass
 * its own wrappers to reach the host kernel.
 */

/* Suppress deprecation warnings for intentional syscall() usage */
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"

int host_open_impl(const char *path, int flags, uint32_t mode) {
    int host_flags = host_translate_open_flags_impl(flags, O_RDONLY, O_WRONLY, O_RDWR,
                                                    O_CREAT, O_EXCL, O_TRUNC, O_APPEND,
                                                    O_NONBLOCK, O_DIRECTORY, O_NOFOLLOW);
    int ret = syscall(SYS_open_nocancel, path, host_flags, (mode_t)mode);
    if (ret < 0) {
        int host_errno = errno;
        errno = host_errno;
        return -1;
    }
    return ret;
}

int host_close_impl(int fd) {
    int ret = syscall(SYS_close_nocancel, fd);
    if (ret < 0) {
        int host_errno = errno;
        errno = host_errno;
        return -1;
    }
    return ret;
}

int host_dup_impl(int fd) {
    int ret = syscall(SYS_dup, fd);
    if (ret < 0) {
        int host_errno = errno;
        errno = host_errno;
        return -1;
    }
    return ret;
}

/* Note: host_stat_impl, host_lstat_impl, host_access_impl are now in path_host.c */

static void translate_host_stat_to_linux(const struct stat *host_stat, struct linux_stat *linux_stat) {
    memset(linux_stat, 0, sizeof(*linux_stat));
    linux_stat->st_dev = host_stat->st_dev;
    linux_stat->st_ino = host_stat->st_ino;
    linux_stat->st_mode = host_stat->st_mode;
    linux_stat->st_nlink = host_stat->st_nlink;
    linux_stat->st_uid = host_stat->st_uid;
    linux_stat->st_gid = host_stat->st_gid;
    linux_stat->st_rdev = host_stat->st_rdev;
    linux_stat->st_size = host_stat->st_size;
    linux_stat->st_blksize = host_stat->st_blksize;
    linux_stat->st_blocks = host_stat->st_blocks;
    linux_stat->st_atime_sec = host_stat->st_atimespec.tv_sec;
    linux_stat->st_atime_nsec = (unsigned long long)host_stat->st_atimespec.tv_nsec;
    linux_stat->st_mtime_sec = host_stat->st_mtimespec.tv_sec;
    linux_stat->st_mtime_nsec = (unsigned long long)host_stat->st_mtimespec.tv_nsec;
    linux_stat->st_ctime_sec = host_stat->st_ctimespec.tv_sec;
    linux_stat->st_ctime_nsec = (unsigned long long)host_stat->st_ctimespec.tv_nsec;
}

int host_fstat_impl(int fd, struct linux_stat *statbuf) {
    struct stat host_stat;
    int ret;

    if (statbuf == NULL) {
        return -host_errno_to_linux_errno(EFAULT);
    }

    ret = syscall(SYS_fstat64, fd, &host_stat);
    if (ret < 0) {
        int host_errno = errno;
        return -host_errno_to_linux_errno(host_errno);
    }

    translate_host_stat_to_linux(&host_stat, statbuf);
    return 0;
}

ssize_t host_read_impl(int fd, void *buf, size_t count) {
    ssize_t ret = syscall(SYS_read_nocancel, fd, buf, count);
    if (ret < 0) {
        int host_errno = errno;
        errno = host_errno;
        return -1;
    }
    return ret;
}

ssize_t host_write_impl(int fd, const void *buf, size_t count) {
    ssize_t ret = syscall(SYS_write_nocancel, fd, buf, count);
    if (ret < 0) {
        int host_errno = errno;
        errno = host_errno;
        return -1;
    }
    return ret;
}

int64_t host_lseek_impl(int fd, int64_t offset, int whence) {
    off_t ret = syscall(SYS_lseek, fd, (off_t)offset, whence);
    if (ret < 0) {
        int host_errno = errno;
        errno = host_errno;
        return -1;
    }
    return ret;
}

int64_t host_pread_impl(int fd, void *buf, size_t count, int64_t offset) {
    ssize_t ret = syscall(SYS_pread_nocancel, fd, buf, count, (off_t)offset);
    if (ret < 0) {
        int host_errno = errno;
        if (host_errno == ENOTSUP) {
            off_t original = syscall(SYS_lseek, fd, 0, SEEK_CUR);
            if (original < 0) {
                return -1;
            }
            if (syscall(SYS_lseek, fd, (off_t)offset, SEEK_SET) < 0) {
                return -1;
            }
            ret = syscall(SYS_read_nocancel, fd, buf, count);
            int saved_errno = errno;
            if (syscall(SYS_lseek, fd, original, SEEK_SET) < 0 && ret >= 0) {
                return -1;
            }
            if (ret < 0) {
                errno = saved_errno;
                return -1;
            }
            return ret;
        }
        errno = host_errno;
        return -1;
    }
    return ret;
}

int64_t host_pwrite_impl(int fd, const void *buf, size_t count, int64_t offset) {
    ssize_t ret = syscall(SYS_pwrite_nocancel, fd, buf, count, (off_t)offset);
    if (ret < 0) {
        int host_errno = errno;
        if (host_errno == ENOTSUP) {
            off_t original = syscall(SYS_lseek, fd, 0, SEEK_CUR);
            if (original < 0) {
                return -1;
            }
            if (syscall(SYS_lseek, fd, (off_t)offset, SEEK_SET) < 0) {
                return -1;
            }
            ret = syscall(SYS_write_nocancel, fd, buf, count);
            int saved_errno = errno;
            if (syscall(SYS_lseek, fd, original, SEEK_SET) < 0 && ret >= 0) {
                return -1;
            }
            if (ret < 0) {
                errno = saved_errno;
                return -1;
            }
            return ret;
        }
        errno = host_errno;
        return -1;
    }
    return ret;
}

ssize_t host_readv_impl(int fd, const struct iovec *iov, int iovcnt) {
    ssize_t ret = syscall(SYS_readv_nocancel, fd, iov, iovcnt);
    if (ret < 0) {
        int host_errno = errno;
        errno = host_errno;
        return -1;
    }
    return ret;
}

ssize_t host_writev_impl(int fd, const struct iovec *iov, int iovcnt) {
    ssize_t ret = syscall(SYS_writev_nocancel, fd, iov, iovcnt);
    if (ret < 0) {
        int host_errno = errno;
        errno = host_errno;
        return -1;
    }
    return ret;
}

int host_poll_impl(struct pollfd *fds, nfds_t nfds, int timeout) {
    int ret = syscall(SYS_poll, fds, nfds, timeout);
    if (ret < 0) {
        int host_errno = errno;
        errno = host_errno;
        return -1;
    }
    return ret;
}

int host_ioctl_impl(int fd, unsigned long request, void *arg) {
    int ret = syscall(SYS_ioctl, fd, request, arg);
    if (ret < 0) {
        int host_errno = errno;
        errno = host_errno;
        return -1;
    }
    return ret;
}

int host_truncate_impl(const char *path, int64_t length) {
    int ret = syscall(SYS_truncate, path, (off_t)length);
    if (ret < 0) {
        int host_errno = errno;
        errno = host_errno;
        return -1;
    }
    return ret;
}

int host_ftruncate_impl(int fd, int64_t length) {
    int ret = syscall(SYS_ftruncate, fd, (off_t)length);
    if (ret < 0) {
        int host_errno = errno;
        errno = host_errno;
        return -1;
    }
    return ret;
}

int host_fcntl_impl(int fd, int cmd, ...) {
    va_list args;
    va_start(args, cmd);
    int arg = va_arg(args, int);
    va_end(args);
    int ret = syscall(SYS_fcntl, fd, cmd, arg);
    if (ret < 0) {
        int host_errno = errno;
        errno = host_errno;
        return -1;
    }
    return ret;
}

#pragma clang diagnostic pop

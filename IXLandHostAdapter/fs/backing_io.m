#include "backing_io_internal.h"

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "errno_translation.h"
#include "fs/stat_types.h"

extern int translate_open_flags(int flags,
                                int darwin_rdonly,
                                int darwin_wronly,
                                int darwin_rdwr,
                                int darwin_creat,
                                int darwin_excl,
                                int darwin_trunc,
                                int darwin_append,
                                int darwin_nonblock,
                                int darwin_directory,
                                int darwin_nofollow);

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

int backing_open(const char *path, int flags, uint32_t mode) {
    int darwin_flags = translate_open_flags(flags, O_RDONLY, O_WRONLY, O_RDWR,
                                            O_CREAT, O_EXCL, O_TRUNC, O_APPEND,
                                            O_NONBLOCK, O_DIRECTORY, O_NOFOLLOW);
    int ret = syscall(SYS_openat, AT_FDCWD, path, darwin_flags, (mode_t)mode);
    if (ret < 0) {
        int darwin_errno = errno;
        errno = darwin_errno;
        return -1;
    }
    return ret;
}

int backing_close(int fd) {
    int ret = syscall(SYS_close_nocancel, fd);
    if (ret < 0) {
        int darwin_errno = errno;
        errno = darwin_errno;
        return -1;
    }
    return ret;
}

int backing_dup(int fd) {
    int ret = syscall(SYS_dup, fd);
    if (ret < 0) {
        int darwin_errno = errno;
        errno = darwin_errno;
        return -1;
    }
    return ret;
}

/* Note: backing_stat, backing_lstat, backing_access are implemented in path.c. */

static void translate_darwin_stat_to_linux(const struct stat *darwin_stat, struct linux_stat *linux_stat) {
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
    linux_stat->st_atime_sec = darwin_stat->st_atimespec.tv_sec;
    linux_stat->st_atime_nsec = (unsigned long long)darwin_stat->st_atimespec.tv_nsec;
    linux_stat->st_mtime_sec = darwin_stat->st_mtimespec.tv_sec;
    linux_stat->st_mtime_nsec = (unsigned long long)darwin_stat->st_mtimespec.tv_nsec;
    linux_stat->st_ctime_sec = darwin_stat->st_ctimespec.tv_sec;
    linux_stat->st_ctime_nsec = (unsigned long long)darwin_stat->st_ctimespec.tv_nsec;
}

int backing_fstat(int fd, struct linux_stat *statbuf) {
    struct stat darwin_stat;
    int ret;

    if (statbuf == NULL) {
        return -linux_errno_from_darwin_errno(EFAULT);
    }

    ret = syscall(SYS_fstat64, fd, &darwin_stat);
    if (ret < 0) {
        int darwin_errno = errno;
        return -linux_errno_from_darwin_errno(darwin_errno);
    }

    translate_darwin_stat_to_linux(&darwin_stat, statbuf);
    return 0;
}

ssize_t backing_read(int fd, void *buf, size_t count) {
    ssize_t ret = syscall(SYS_read, fd, buf, count);
    if (ret < 0) {
        int darwin_errno = errno;
        errno = darwin_errno;
        return -1;
    }
    return ret;
}

ssize_t backing_write(int fd, const void *buf, size_t count) {
    ssize_t ret = syscall(SYS_write, fd, buf, count);
    if (ret < 0) {
        int darwin_errno = errno;
        errno = darwin_errno;
        return -1;
    }
    return ret;
}

int64_t backing_lseek(int fd, int64_t offset, int whence) {
    off_t ret = syscall(SYS_lseek, fd, (off_t)offset, whence);
    if (ret < 0) {
        int darwin_errno = errno;
        errno = darwin_errno;
        return -1;
    }
    return ret;
}

int64_t backing_pread(int fd, void *buf, size_t count, int64_t offset) {
    ssize_t ret = syscall(SYS_pread_nocancel, fd, buf, count, (off_t)offset);
    if (ret < 0) {
        int darwin_errno = errno;
        if (darwin_errno == ENOTSUP) {
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
        errno = darwin_errno;
        return -1;
    }
    return ret;
}

int64_t backing_pwrite(int fd, const void *buf, size_t count, int64_t offset) {
    ssize_t ret = syscall(SYS_pwrite_nocancel, fd, buf, count, (off_t)offset);
    if (ret < 0) {
        int darwin_errno = errno;
        if (darwin_errno == ENOTSUP) {
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
        errno = darwin_errno;
        return -1;
    }
    return ret;
}

ssize_t backing_readv(int fd, const struct iovec *iov, int iovcnt) {
    ssize_t ret = syscall(SYS_readv_nocancel, fd, iov, iovcnt);
    if (ret < 0) {
        int darwin_errno = errno;
        errno = darwin_errno;
        return -1;
    }
    return ret;
}

ssize_t backing_writev(int fd, const struct iovec *iov, int iovcnt) {
    ssize_t ret = syscall(SYS_writev_nocancel, fd, iov, iovcnt);
    if (ret < 0) {
        int darwin_errno = errno;
        errno = darwin_errno;
        return -1;
    }
    return ret;
}

int backing_poll(struct pollfd *fds, nfds_t nfds, int timeout) {
    int ret = syscall(SYS_poll, fds, nfds, timeout);
    if (ret < 0) {
        int darwin_errno = errno;
        errno = darwin_errno;
        return -1;
    }
    return ret;
}

int backing_ioctl(int fd, unsigned long request, void *arg) {
    int ret = syscall(SYS_ioctl, fd, request, arg);
    if (ret < 0) {
        int darwin_errno = errno;
        errno = darwin_errno;
        return -1;
    }
    return ret;
}

int backing_truncate(const char *path, int64_t length) {
    int ret = syscall(SYS_truncate, path, (off_t)length);
    if (ret < 0) {
        int darwin_errno = errno;
        errno = darwin_errno;
        return -1;
    }
    return ret;
}

int backing_ftruncate(int fd, int64_t length) {
    int ret = syscall(SYS_ftruncate, fd, (off_t)length);
    if (ret < 0) {
        int darwin_errno = errno;
        errno = darwin_errno;
        return -1;
    }
    return ret;
}

int backing_fcntl(int fd, int cmd, ...) {
    va_list args;
    va_start(args, cmd);
    int arg = va_arg(args, int);
    va_end(args);
    int ret = syscall(SYS_fcntl, fd, cmd, arg);
    if (ret < 0) {
        int darwin_errno = errno;
        errno = darwin_errno;
        return -1;
    }
    return ret;
}

#pragma clang diagnostic pop

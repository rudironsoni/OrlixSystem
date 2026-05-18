#include "backing_io_internal.h"
#include "backing_stat_translate.h"

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <sys/stat.h>

#include "errno_translation.h"

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
 * operations without recursion through Orlix's exported wrappers.
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
        return -linux_errno_from_darwin_errno(errno);
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

int backing_fstat(int fd, struct stat *statbuf) {
    struct stat darwin_stat;
    struct backing_stat_data data;
    int ret;

    if (statbuf == NULL) {
        return -linux_errno_from_darwin_errno(EFAULT);
    }

    ret = syscall(SYS_fstat64, fd, &darwin_stat);
    if (ret < 0) {
        int darwin_errno = errno;
        return -linux_errno_from_darwin_errno(darwin_errno);
    }

    capture_backing_stat(&darwin_stat, &data);
    backing_stat_translate(&data, statbuf);
    return 0;
}

ssize_t backing_read(int fd, void *buf, size_t count) {
    ssize_t ret = syscall(SYS_read, fd, buf, count);
    if (ret < 0) {
        return -linux_errno_from_darwin_errno(errno);
    }
    return ret;
}

ssize_t backing_write(int fd, const void *buf, size_t count) {
    ssize_t ret = syscall(SYS_write, fd, buf, count);
    if (ret < 0) {
        return -linux_errno_from_darwin_errno(errno);
    }
    return ret;
}

int64_t backing_lseek(int fd, int64_t offset, int whence) {
    off_t ret = syscall(SYS_lseek, fd, (off_t)offset, whence);
    if (ret < 0) {
        return -linux_errno_from_darwin_errno(errno);
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
                return -linux_errno_from_darwin_errno(errno);
            }
            if (syscall(SYS_lseek, fd, (off_t)offset, SEEK_SET) < 0) {
                return -linux_errno_from_darwin_errno(errno);
            }
            ret = syscall(SYS_read_nocancel, fd, buf, count);
            int saved_errno = errno;
            if (syscall(SYS_lseek, fd, original, SEEK_SET) < 0 && ret >= 0) {
                return -linux_errno_from_darwin_errno(errno);
            }
            if (ret < 0) {
                return -linux_errno_from_darwin_errno(saved_errno);
            }
            return ret;
        }
        return -linux_errno_from_darwin_errno(darwin_errno);
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
                return -linux_errno_from_darwin_errno(errno);
            }
            if (syscall(SYS_lseek, fd, (off_t)offset, SEEK_SET) < 0) {
                return -linux_errno_from_darwin_errno(errno);
            }
            ret = syscall(SYS_write_nocancel, fd, buf, count);
            int saved_errno = errno;
            if (syscall(SYS_lseek, fd, original, SEEK_SET) < 0 && ret >= 0) {
                return -linux_errno_from_darwin_errno(errno);
            }
            if (ret < 0) {
                return -linux_errno_from_darwin_errno(saved_errno);
            }
            return ret;
        }
        return -linux_errno_from_darwin_errno(darwin_errno);
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
        return -linux_errno_from_darwin_errno(errno);
    }
    return ret;
}

int backing_truncate(const char *path, int64_t length) {
    int ret = syscall(SYS_truncate, path, (off_t)length);
    if (ret < 0) {
        return -linux_errno_from_darwin_errno(errno);
    }
    return ret;
}

int backing_ftruncate(int fd, int64_t length) {
    int ret = syscall(SYS_ftruncate, fd, (off_t)length);
    if (ret < 0) {
        return -linux_errno_from_darwin_errno(errno);
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

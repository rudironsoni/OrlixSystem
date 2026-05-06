#include <linux/uio.h>

#include <errno.h>
#include <stddef.h>
#include <stdint.h>

#include "fs/vfs.h"

extern long read_impl(int fd, void *buf, size_t count);
extern long write_impl(int fd, const void *buf, size_t count);
extern linux_ssize_t pread_impl(int fd, void *buf, size_t count, long long offset);
extern linux_ssize_t pwrite_impl(int fd, const void *buf, size_t count, long long offset);
extern linux_ssize_t copy_file_range_impl(int fd_in, long long *off_in, int fd_out,
                                          long long *off_out, size_t len, unsigned int flags);

static long long uio_combine_offset(unsigned long pos_l, unsigned long pos_h) {
    return (long long)(((uint64_t)pos_h << 32) | (uint64_t)(uint32_t)pos_l);
}

static long uio_preadv_common(int fd,
                              const struct iovec *iov,
                              int iovcnt,
                              long long offset) {
    long total = 0;

    if (offset < 0) {
        errno = EINVAL;
        return -1;
    }
    if (iovcnt < 0 || iovcnt > UIO_MAXIOV) {
        errno = EINVAL;
        return -1;
    }
    if (iovcnt == 0) {
        return 0;
    }
    if (!iov) {
        errno = EFAULT;
        return -1;
    }

    for (int i = 0; i < iovcnt; i++) {
        long nread;

        if (iov[i].iov_len != 0 && !iov[i].iov_base) {
            errno = EFAULT;
            return total > 0 ? total : -1;
        }
        nread = pread_impl(fd, iov[i].iov_base, iov[i].iov_len, offset);
        if (nread < 0) {
            return total > 0 ? total : -1;
        }
        total += nread;
        offset += nread;
        if ((__kernel_size_t)nread < iov[i].iov_len) {
            break;
        }
    }

    return total;
}

static long uio_pwritev_common(int fd,
                               const struct iovec *iov,
                               int iovcnt,
                               long long offset) {
    long total = 0;

    if (offset < 0) {
        errno = EINVAL;
        return -1;
    }
    if (iovcnt < 0 || iovcnt > UIO_MAXIOV) {
        errno = EINVAL;
        return -1;
    }
    if (iovcnt == 0) {
        return 0;
    }
    if (!iov) {
        errno = EFAULT;
        return -1;
    }

    for (int i = 0; i < iovcnt; i++) {
        long nwritten;

        if (iov[i].iov_len != 0 && !iov[i].iov_base) {
            errno = EFAULT;
            return total > 0 ? total : -1;
        }
        nwritten = pwrite_impl(fd, iov[i].iov_base, iov[i].iov_len, offset);
        if (nwritten < 0) {
            return total > 0 ? total : -1;
        }
        total += nwritten;
        offset += nwritten;
        if ((__kernel_size_t)nwritten < iov[i].iov_len) {
            break;
        }
    }

    return total;
}

long readv_impl(int fd, const struct iovec *iov, int iovcnt) {
    long total = 0;

    if (iovcnt < 0 || iovcnt > UIO_MAXIOV) {
        errno = EINVAL;
        return -1;
    }
    if (iovcnt == 0) {
        return 0;
    }
    if (!iov) {
        errno = EFAULT;
        return -1;
    }

    for (int i = 0; i < iovcnt; i++) {
        long nread;

        if (iov[i].iov_len != 0 && !iov[i].iov_base) {
            errno = EFAULT;
            return total > 0 ? total : -1;
        }
        nread = read_impl(fd, iov[i].iov_base, iov[i].iov_len);
        if (nread < 0) {
            return total > 0 ? total : -1;
        }
        total += nread;
        if ((__kernel_size_t)nread < iov[i].iov_len) {
            break;
        }
    }

    return total;
}

long writev_impl(int fd, const struct iovec *iov, int iovcnt) {
    long total = 0;

    if (iovcnt < 0 || iovcnt > UIO_MAXIOV) {
        errno = EINVAL;
        return -1;
    }
    if (iovcnt == 0) {
        return 0;
    }
    if (!iov) {
        errno = EFAULT;
        return -1;
    }

    for (int i = 0; i < iovcnt; i++) {
        long nwritten;

        if (iov[i].iov_len != 0 && !iov[i].iov_base) {
            errno = EFAULT;
            return total > 0 ? total : -1;
        }
        nwritten = write_impl(fd, iov[i].iov_base, iov[i].iov_len);
        if (nwritten < 0) {
            return total > 0 ? total : -1;
        }
        total += nwritten;
        if ((__kernel_size_t)nwritten < iov[i].iov_len) {
            break;
        }
    }

    return total;
}

__attribute__((visibility("default"))) long readv(int fd, const struct iovec *iov, int iovcnt) {
    return readv_impl(fd, iov, iovcnt);
}

__attribute__((visibility("default"))) long writev(int fd, const struct iovec *iov, int iovcnt) {
    return writev_impl(fd, iov, iovcnt);
}

long preadv_impl(int fd, const struct iovec *iov, int iovcnt, unsigned long pos_l, unsigned long pos_h) {
    return uio_preadv_common(fd, iov, iovcnt, uio_combine_offset(pos_l, pos_h));
}

long pwritev_impl(int fd, const struct iovec *iov, int iovcnt, unsigned long pos_l, unsigned long pos_h) {
    return uio_pwritev_common(fd, iov, iovcnt, uio_combine_offset(pos_l, pos_h));
}

long preadv2_impl(int fd,
                  const struct iovec *iov,
                  int iovcnt,
                  unsigned long pos_l,
                  unsigned long pos_h,
                  int flags) {
    if (flags != 0) {
        errno = EOPNOTSUPP;
        return -1;
    }
    return uio_preadv_common(fd, iov, iovcnt, uio_combine_offset(pos_l, pos_h));
}

long pwritev2_impl(int fd,
                   const struct iovec *iov,
                   int iovcnt,
                   unsigned long pos_l,
                   unsigned long pos_h,
                   int flags) {
    if (flags != 0) {
        errno = EOPNOTSUPP;
        return -1;
    }
    return uio_pwritev_common(fd, iov, iovcnt, uio_combine_offset(pos_l, pos_h));
}

linux_ssize_t sendfile_impl(int out_fd, int in_fd, long long *offset, size_t count) {
    long long in_offset = 0;
    linux_ssize_t copied;

    if (offset) {
        in_offset = *offset;
    }
    copied = copy_file_range_impl(in_fd, offset ? &in_offset : NULL, out_fd, NULL, count, 0);
    if (copied >= 0 && offset) {
        *offset = in_offset;
    }
    return copied;
}

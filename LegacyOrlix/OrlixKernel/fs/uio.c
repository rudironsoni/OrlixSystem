#include <linux/uio.h>

#include <linux/errno.h>
#include <linux/types.h>

#include "read_write.h"
#include "fs/vfs.h"

static long long uio_combine_offset(unsigned long pos_l, unsigned long pos_h) {
    return (long long)(((uint64_t)pos_h << 32) | (uint64_t)(uint32_t)pos_l);
}

static long uio_preadv_common(int fd,
                              const struct iovec *iov,
                              int iovcnt,
                              long long offset) {
    long total = 0;

    if (offset < 0) {
        return -EINVAL;
    }
    if (iovcnt < 0 || iovcnt > UIO_MAXIOV) {
        return -EINVAL;
    }
    if (iovcnt == 0) {
        return 0;
    }
    if (!iov) {
        return -EFAULT;
    }

    for (int i = 0; i < iovcnt; i++) {
        long nread;

        if (iov[i].iov_len != 0 && !iov[i].iov_base) {
            return total > 0 ? total : -EFAULT;
        }
        nread = pread_impl(fd, iov[i].iov_base, iov[i].iov_len, offset);
        if (nread < 0) {
            return total > 0 ? total : nread;
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
        return -EINVAL;
    }
    if (iovcnt < 0 || iovcnt > UIO_MAXIOV) {
        return -EINVAL;
    }
    if (iovcnt == 0) {
        return 0;
    }
    if (!iov) {
        return -EFAULT;
    }

    for (int i = 0; i < iovcnt; i++) {
        long nwritten;

        if (iov[i].iov_len != 0 && !iov[i].iov_base) {
            return total > 0 ? total : -EFAULT;
        }
        nwritten = pwrite_impl(fd, iov[i].iov_base, iov[i].iov_len, offset);
        if (nwritten < 0) {
            return total > 0 ? total : nwritten;
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
        return -EINVAL;
    }
    if (iovcnt == 0) {
        return 0;
    }
    if (!iov) {
        return -EFAULT;
    }

    for (int i = 0; i < iovcnt; i++) {
        long nread;

        if (iov[i].iov_len != 0 && !iov[i].iov_base) {
            return total > 0 ? total : -EFAULT;
        }
        nread = read_impl(fd, iov[i].iov_base, iov[i].iov_len);
        if (nread < 0) {
            return total > 0 ? total : nread;
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
        return -EINVAL;
    }
    if (iovcnt == 0) {
        return 0;
    }
    if (!iov) {
        return -EFAULT;
    }

    for (int i = 0; i < iovcnt; i++) {
        long nwritten;

        if (iov[i].iov_len != 0 && !iov[i].iov_base) {
            return total > 0 ? total : -EFAULT;
        }
        nwritten = write_impl(fd, iov[i].iov_base, iov[i].iov_len);
        if (nwritten < 0) {
            return total > 0 ? total : nwritten;
        }
        total += nwritten;
        if ((__kernel_size_t)nwritten < iov[i].iov_len) {
            break;
        }
    }

    return total;
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
        return -EOPNOTSUPP;
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
        return -EOPNOTSUPP;
    }
    return uio_pwritev_common(fd, iov, iovcnt, uio_combine_offset(pos_l, pos_h));
}

int64_t sendfile_impl(int out_fd, int in_fd, long long *offset, size_t count) {
    long long in_offset = 0;
    int64_t copied;

    if (offset) {
        in_offset = *offset;
    }
    copied = copy_file_range_impl(in_fd, offset ? &in_offset : NULL, out_fd, NULL, count, 0);
    if (copied >= 0 && offset) {
        *offset = in_offset;
    }
    return copied;
}

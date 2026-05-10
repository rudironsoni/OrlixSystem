#include <errno.h>
#include <stddef.h>
#include <sys/types.h>

#include <linux/uio.h>

extern long readv_impl(int fd, const struct iovec *iov, int iovcnt);
extern long writev_impl(int fd, const struct iovec *iov, int iovcnt);
extern long preadv_impl(int fd, const struct iovec *iov, int iovcnt, unsigned long pos_l, unsigned long pos_h);
extern long pwritev_impl(int fd, const struct iovec *iov, int iovcnt, unsigned long pos_l, unsigned long pos_h);
extern long preadv2_impl(int fd, const struct iovec *iov, int iovcnt,
                         unsigned long pos_l, unsigned long pos_h, int flags);
extern long pwritev2_impl(int fd, const struct iovec *iov, int iovcnt,
                          unsigned long pos_l, unsigned long pos_h, int flags);

static ssize_t wrap_ssize_result(long ret) {
    if (ret < 0) {
        errno = (int)-ret;
        return -1;
    }
    return (ssize_t)ret;
}

__attribute__((visibility("default"))) ssize_t readv(int fd, const struct iovec *iov, int iovcnt) {
    return wrap_ssize_result(readv_impl(fd, iov, iovcnt));
}

__attribute__((visibility("default"))) ssize_t writev(int fd, const struct iovec *iov, int iovcnt) {
    return wrap_ssize_result(writev_impl(fd, iov, iovcnt));
}

__attribute__((visibility("default"))) ssize_t preadv(int fd, const struct iovec *iov, int iovcnt, long long offset) {
    return wrap_ssize_result(preadv_impl(fd, iov, iovcnt, (unsigned long)offset, (unsigned long)((unsigned long long)offset >> 32)));
}

__attribute__((visibility("default"))) ssize_t pwritev(int fd, const struct iovec *iov, int iovcnt, long long offset) {
    return wrap_ssize_result(pwritev_impl(fd, iov, iovcnt, (unsigned long)offset, (unsigned long)((unsigned long long)offset >> 32)));
}

__attribute__((visibility("default"))) ssize_t preadv2(int fd, const struct iovec *iov, int iovcnt, long long offset, int flags) {
    return wrap_ssize_result(preadv2_impl(fd, iov, iovcnt, (unsigned long)offset, (unsigned long)((unsigned long long)offset >> 32), flags));
}

__attribute__((visibility("default"))) ssize_t pwritev2(int fd, const struct iovec *iov, int iovcnt, long long offset, int flags) {
    return wrap_ssize_result(pwritev2_impl(fd, iov, iovcnt, (unsigned long)offset, (unsigned long)((unsigned long long)offset >> 32), flags));
}

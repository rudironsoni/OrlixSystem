#ifndef FS_UIO_H
#define FS_UIO_H

#include <linux/types.h>
#include <linux/uio.h>

#ifdef __cplusplus
extern "C" {
#endif

long readv_impl(int fd, const struct iovec *iov, int iovcnt);
long writev_impl(int fd, const struct iovec *iov, int iovcnt);
long preadv_impl(int fd, const struct iovec *iov, int iovcnt, unsigned long pos_l, unsigned long pos_h);
long pwritev_impl(int fd, const struct iovec *iov, int iovcnt, unsigned long pos_l, unsigned long pos_h);
long preadv2_impl(int fd, const struct iovec *iov, int iovcnt,
                  unsigned long pos_l, unsigned long pos_h, int flags);
long pwritev2_impl(int fd, const struct iovec *iov, int iovcnt,
                   unsigned long pos_l, unsigned long pos_h, int flags);
int64_t sendfile_impl(int out_fd, int in_fd, long long *offset, size_t count);

#ifdef __cplusplus
}
#endif

#endif

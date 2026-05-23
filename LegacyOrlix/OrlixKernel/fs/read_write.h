#ifndef FS_READ_WRITE_H
#define FS_READ_WRITE_H

#include <linux/types.h>
#include <linux/uio.h>

#ifdef __cplusplus
extern "C" {
#endif

ssize_t read_impl(int fd, void *buf, size_t count);
ssize_t write_impl(int fd, const void *buf, size_t count);
int64_t lseek_impl(int fd, int64_t offset, int whence);
ssize_t pread_impl(int fd, void *buf, size_t count, int64_t offset);
ssize_t pwrite_impl(int fd, const void *buf, size_t count, int64_t offset);
ssize_t copy_file_range_impl(int fd_in, int64_t *off_in, int fd_out,
                             int64_t *off_out, size_t len, unsigned int flags);
int fallocate_impl(int fd, int mode, int64_t offset, int64_t len);
int sync_file_range_impl(int fd, int64_t offset, int64_t nbytes, unsigned int flags);
ssize_t splice_impl(int fd_in, int64_t *off_in, int fd_out, int64_t *off_out,
                    size_t len, unsigned int flags);
ssize_t vmsplice_impl(int fd, const struct iovec *iov, unsigned long nr_segs, unsigned int flags);
ssize_t tee_impl(int fd_in, int fd_out, size_t len, unsigned int flags);

#ifdef __cplusplus
}
#endif

#endif

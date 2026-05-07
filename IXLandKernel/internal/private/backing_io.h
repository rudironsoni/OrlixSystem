#ifndef IXLAND_INTERNAL_PRIVATE_BACKING_IO_H
#define IXLAND_INTERNAL_PRIVATE_BACKING_IO_H

#include <stdint.h>
#include <sys/types.h>

struct iovec;
struct linux_stat;

int backing_open(const char *path, int flags, uint32_t mode);
int backing_close(int fd);
int backing_dup(int fd);
int backing_fstat(int fd, struct linux_stat *statbuf);
ssize_t backing_read(int fd, void *buf, size_t count);
ssize_t backing_write(int fd, const void *buf, size_t count);
int64_t backing_lseek(int fd, int64_t offset, int whence);
ssize_t backing_pread(int fd, void *buf, size_t count, int64_t offset);
ssize_t backing_pwrite(int fd, const void *buf, size_t count, int64_t offset);
ssize_t backing_readv(int fd, const struct iovec *iov, int iovcnt);
ssize_t backing_writev(int fd, const struct iovec *iov, int iovcnt);
int backing_truncate(const char *path, int64_t length);
int backing_ftruncate(int fd, int64_t length);
int backing_ensure_directory(const char *path, uint32_t mode);
int backing_fcntl(int fd, int cmd, ...);

#endif

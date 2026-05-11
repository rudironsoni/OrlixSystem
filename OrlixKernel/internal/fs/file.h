#ifndef INTERNAL_FS_FILE_H
#define INTERNAL_FS_FILE_H

#include <linux/stddef.h>
#include <linux/types.h>

struct iovec;
struct stat;

int backing_open(const char *path, int flags, uint32_t mode);
int backing_close(int fd);
int backing_dup(int fd);
int backing_fstat(int fd, struct stat *statbuf);
long backing_read(int fd, void *buf, size_t count);
long backing_write(int fd, const void *buf, size_t count);
int64_t backing_lseek(int fd, int64_t offset, int whence);
long backing_pread(int fd, void *buf, size_t count, int64_t offset);
long backing_pwrite(int fd, const void *buf, size_t count, int64_t offset);
long backing_readv(int fd, const struct iovec *iov, int iovcnt);
long backing_writev(int fd, const struct iovec *iov, int iovcnt);
int backing_truncate(const char *path, int64_t length);
int backing_ftruncate(int fd, int64_t length);
int backing_ensure_directory(const char *path, uint32_t mode);
int backing_fcntl(int fd, int cmd, ...);

#endif

#ifndef BACKING_IO_DECLS_H
#define BACKING_IO_DECLS_H

/* Host backing I/O function declarations for Linux-owner code
 *
 * This header provides ONLY function declarations - no implementations,
 * no Darwin headers, no macro definitions.
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations for types defined elsewhere */
struct linux_stat;
struct iovec;
struct pollfd;
struct timespec;

/* Linux-sized types for the API */
typedef int64_t linux_off_t;
typedef uint32_t linux_mode_t;

/* Host container path discovery */
int vfs_discover_persistent_root(char *path, size_t path_len);
int vfs_discover_cache_root(char *path, size_t path_len);
int vfs_discover_temp_root(char *path, size_t path_len);

/* Host filesystem operations
 *
 * These functions accept Linux UAPI constant values (e.g., O_CREAT=0x40).
 * The internal iOS implementation translates to host values.
 */
int host_open_impl(const char *path, int flags, linux_mode_t mode);
int host_close_impl(int fd);
int host_dup_impl(int fd);
int host_stat_impl(const char *path, struct linux_stat *statbuf);
int host_lstat_impl(const char *path, struct linux_stat *statbuf);
int host_access_impl(const char *path, int mode);
int host_fstat_impl(int fd, struct linux_stat *statbuf);
int64_t host_read_impl(int fd, void *buf, size_t count);
int64_t host_write_impl(int fd, const void *buf, size_t count);
linux_off_t host_lseek_impl(int fd, linux_off_t offset, int whence);
int64_t host_pread_impl(int fd, void *buf, size_t count, linux_off_t offset);
int64_t host_pwrite_impl(int fd, const void *buf, size_t count, linux_off_t offset);
int64_t host_readv_impl(int fd, const struct iovec *iov, int iovcnt);
int64_t host_writev_impl(int fd, const struct iovec *iov, int iovcnt);
int host_poll_impl(struct pollfd *fds, unsigned int nfds, int timeout);
int host_ioctl_impl(int fd, unsigned long request, void *arg);
int host_truncate_impl(const char *path, linux_off_t length);
int host_ftruncate_impl(int fd, linux_off_t length);
int host_ensure_directory_impl(const char *path, linux_mode_t mode);
int host_fcntl_impl(int fd, int cmd, ...);

#ifdef __cplusplus
}
#endif

#endif

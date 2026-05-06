/* internal/ios/fs/file_io_host.h
 * Narrow seam for file I/O host operations
 */

#ifndef FILE_IO_HOST_H
#define FILE_IO_HOST_H

#include <stddef.h>
#include <stdint.h>

/* Include shared stat type definition */
#include "fs/stat_types.h"
#include "backing_io_decls.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Linux-sized types for the API */
typedef int64_t linux_off_t;
typedef uint32_t linux_mode_t;

/* File I/O operations */
int host_open_impl(const char *path, int flags, linux_mode_t mode);
int host_close_impl(int fd);
int host_dup_impl(int fd);
int64_t host_read_impl(int fd, void *buf, size_t count);
int64_t host_write_impl(int fd, const void *buf, size_t count);
linux_off_t host_lseek_impl(int fd, linux_off_t offset, int whence);
int64_t host_pread_impl(int fd, void *buf, size_t count, linux_off_t offset);
int64_t host_pwrite_impl(int fd, const void *buf, size_t count, linux_off_t offset);
int host_fcntl_impl(int fd, int cmd, ...);

/* Stat operations - use Linux stat type */
int host_stat_impl(const char *path, struct linux_stat *statbuf);
int host_fstat_impl(int fd, struct linux_stat *statbuf);

/* Truncate operations */
int host_truncate_impl(const char *path, linux_off_t length);
int host_ftruncate_impl(int fd, linux_off_t length);

#ifdef __cplusplus
}
#endif

#endif /* FILE_IO_HOST_H */

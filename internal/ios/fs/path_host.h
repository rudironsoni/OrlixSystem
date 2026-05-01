/* internal/ios/fs/path_host.h
 * Narrow seam for host path operations
 *
 * This header provides ONLY function declarations.
 * Darwin headers are NOT included here - they are isolated in path_host.c
 */

#ifndef PATH_HOST_H
#define PATH_HOST_H

#include <stddef.h>
#include <stdint.h>
#include <unistd.h>  /* For ssize_t */

/* Forward declaration - defined in fs/vfs.h for Linux-owner code */
struct linux_stat;

#ifdef __cplusplus
extern "C" {
#endif

/* Host stat operations - use Linux stat type */
int host_stat_impl(const char *path, struct linux_stat *statbuf);
int host_lstat_impl(const char *path, struct linux_stat *statbuf);
int host_access_impl(const char *path, int mode);

/* Host rename operation (Darwin renameatx_np) */
int host_renameatx_np_impl(int fromfd, const char *from, int tofd, const char *to, unsigned int flags);
int host_rename_exchange_impl(const char *from, const char *to);

/* Directory operations */
int host_mkdir_impl(const char *pathname, uint32_t mode);
int host_rmdir_impl(const char *pathname);

/* File operations */
int host_unlink_impl(const char *pathname);
int host_link_impl(const char *oldpath, const char *newpath);
int host_symlink_impl(const char *target, const char *linkpath);
ssize_t host_readlink_impl(const char *pathname, char *buf, size_t bufsiz);

/* Fchdir */
int host_fchdir_impl(int fd);

#ifdef __cplusplus
}
#endif

#endif /* PATH_HOST_H */

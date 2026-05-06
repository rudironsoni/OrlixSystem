#ifndef IXLAND_HOST_ADAPTER_FS_PATH_HOST_H
#define IXLAND_HOST_ADAPTER_FS_PATH_HOST_H

#include <stddef.h>
#include <stdint.h>
#include <unistd.h>

struct linux_stat;

int host_stat_impl(const char *path, struct linux_stat *statbuf);
int host_lstat_impl(const char *path, struct linux_stat *statbuf);
int host_access_impl(const char *path, int mode);
int host_directory_is_empty_impl(const char *path);
int host_renameatx_np_impl(int fromfd, const char *from, int tofd, const char *to, unsigned int flags);
int host_rename_exchange_impl(const char *from, const char *to);
int host_mkdir_impl(const char *pathname, uint32_t mode);
int host_rmdir_impl(const char *pathname);
int host_unlink_impl(const char *pathname);
int host_link_impl(const char *oldpath, const char *newpath);
int host_linkat_impl(const char *oldpath, const char *newpath, int follow_symlink);
int host_symlink_impl(const char *target, const char *linkpath);
ssize_t host_readlink_impl(const char *pathname, char *buf, size_t bufsiz);
int host_fchdir_impl(int fd);

#endif

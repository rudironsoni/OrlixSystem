#ifndef IXLAND_INTERNAL_PRIVATE_BACKING_PATH_H
#define IXLAND_INTERNAL_PRIVATE_BACKING_PATH_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

struct linux_stat;

int backing_stat(const char *path, struct linux_stat *statbuf);
int backing_lstat(const char *path, struct linux_stat *statbuf);
int backing_access(const char *path, int mode);
int backing_directory_is_empty(const char *path);
int backing_rename_with_flags(int fromfd, const char *from, int tofd, const char *to, unsigned int flags);
int backing_rename_exchange(const char *from, const char *to);
int backing_mkdir(const char *pathname, uint32_t mode);
int backing_rmdir(const char *pathname);
int backing_unlink(const char *pathname);
int backing_link(const char *oldpath, const char *newpath);
int backing_linkat(const char *oldpath, const char *newpath, int follow_symlink);
int backing_symlink(const char *target, const char *linkpath);
ssize_t backing_readlink(const char *pathname, char *buf, size_t bufsiz);
int backing_fchdir(int fd);

#endif

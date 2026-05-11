#ifndef INTERNAL_FS_NAMEI_H
#define INTERNAL_FS_NAMEI_H

#include <linux/stddef.h>
#include <linux/types.h>

struct stat;

int backing_stat(const char *path, struct stat *statbuf);
int backing_lstat(const char *path, struct stat *statbuf);
int backing_access(const char *path, int mode);
_Bool backing_path_is_own_sandbox(const char *path);
_Bool backing_path_is_external(const char *path);
int backing_directory_is_empty(const char *path);
int backing_rename_with_flags(int fromfd, const char *from, int tofd, const char *to, unsigned int flags);
int backing_rename_exchange(const char *from, const char *to);
int backing_mkdir(const char *pathname, uint32_t mode);
int backing_rmdir(const char *pathname);
int backing_unlink(const char *pathname);
int backing_link(const char *oldpath, const char *newpath);
int backing_linkat(const char *oldpath, const char *newpath, int follow_symlink);
int backing_symlink(const char *target, const char *linkpath);
long backing_readlink(const char *pathname, char *buf, size_t bufsiz);
int backing_fchdir(int fd);

#endif

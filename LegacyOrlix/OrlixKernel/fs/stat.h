#ifndef FS_STAT_H
#define FS_STAT_H

#include <linux/types.h>
#include <asm/stat.h>

#ifdef __cplusplus
extern "C" {
#endif

int stat_impl(const char *pathname, struct stat *statbuf);
int fstat_impl(int fd, struct stat *statbuf);
int lstat_impl(const char *pathname, struct stat *statbuf);
int access_impl(const char *pathname, int mode);
int fstatat_impl(int dirfd, const char *pathname, struct stat *statbuf, int flags);
int faccessat_impl(int dirfd, const char *pathname, int mode, int flags);
int statx_impl(int dirfd, const char *pathname, int flags, unsigned int mask,
               struct statx *statxbuf);

#ifdef __cplusplus
}
#endif

#endif

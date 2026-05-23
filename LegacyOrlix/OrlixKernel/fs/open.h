#ifndef FS_OPEN_H
#define FS_OPEN_H

#include <linux/types.h>

#ifdef __cplusplus
extern "C" {
#endif

int open_impl(const char *pathname, int flags, mode_t mode);
int openat_impl(int dirfd, const char *pathname, int flags, mode_t mode);
int creat_impl(const char *pathname, mode_t mode);

#ifdef __cplusplus
}
#endif

#endif

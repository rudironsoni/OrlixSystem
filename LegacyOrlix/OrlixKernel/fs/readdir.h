#ifndef FS_READDIR_H
#define FS_READDIR_H

#include <linux/types.h>

#ifdef __cplusplus
extern "C" {
#endif

ssize_t getdents64_impl(int fd, void *dirp, size_t count);
ssize_t getdents_impl(int fd, void *dirp, size_t count);

#ifdef __cplusplus
}
#endif

#endif

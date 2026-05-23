#ifndef FS_FCNTL_H
#define FS_FCNTL_H

#include <linux/types.h>

#ifdef __cplusplus
extern "C" {
#endif

int flock_impl(int fd, int operation);
int dup_impl(int oldfd);
int dup2_impl(int oldfd, int newfd);
int dup3_impl(int oldfd, int newfd, int flags);
int fcntl_impl(int fd, int cmd, ...);

#ifdef __cplusplus
}
#endif

#endif

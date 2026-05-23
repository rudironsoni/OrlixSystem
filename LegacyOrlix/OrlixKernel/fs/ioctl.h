#ifndef FS_IOCTL_H
#define FS_IOCTL_H

#include <linux/types.h>

#ifdef __cplusplus
extern "C" {
#endif

int ioctl_impl(int fd, unsigned long request, void *arg);

#ifdef __cplusplus
}
#endif

#endif

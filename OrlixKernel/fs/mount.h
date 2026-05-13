#ifndef FS_MOUNT_H
#define FS_MOUNT_H

#include <linux/types.h>

#ifdef __cplusplus
extern "C" {
#endif

int mount_impl(const char *source, const char *target,
               const char *filesystemtype, unsigned long mountflags,
               const void *data);
int umount_impl(const char *target);
int umount2_impl(const char *target, int flags);

#ifdef __cplusplus
}
#endif

#endif

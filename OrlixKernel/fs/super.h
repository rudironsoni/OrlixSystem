#ifndef FS_SUPER_H
#define FS_SUPER_H

#include <linux/types.h>
#include <uapi/asm/statfs.h>

#ifdef __cplusplus
extern "C" {
#endif

void sync_impl(void);
int fsync_impl(int fd);
int fdatasync_impl(int fd);
int syncfs_impl(int fd);
int statfs_impl(const char *path, struct statfs *buf);
int fstatfs_impl(int fd, struct statfs *buf);

#ifdef __cplusplus
}
#endif

#endif

#ifndef FS_INODE_H
#define FS_INODE_H

#include <linux/types.h>
#include <linux/time_types.h>

#ifdef __cplusplus
extern "C" {
#endif

int chmod_impl(const char *pathname, uint32_t mode);
int fchmod_impl(int fd, uint32_t mode);
int fchmodat_impl(int dirfd, const char *pathname, uint32_t mode, int flags);
int chown_impl(const char *pathname, uint32_t owner, uint32_t group);
int fchown_impl(int fd, uint32_t owner, uint32_t group);
int fchownat_impl(int dirfd, const char *pathname, uint32_t owner, uint32_t group, int flags);
int utimensat_impl(int dirfd, const char *pathname, const struct __kernel_timespec times[2],
                   int flags);
uint32_t umask_impl(uint32_t mask);
int truncate_impl(const char *path, int64_t length);
int ftruncate_impl(int fd, int64_t length);

#ifdef __cplusplus
}
#endif

#endif

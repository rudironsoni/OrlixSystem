#ifndef FS_XATTR_H
#define FS_XATTR_H

#include <linux/types.h>

#ifdef __cplusplus
extern "C" {
#endif

int setxattr_impl(const char *path, const char *name, const void *value, size_t size, int flags);
int lsetxattr_impl(const char *path, const char *name, const void *value, size_t size, int flags);
int fsetxattr_impl(int fd, const char *name, const void *value, size_t size, int flags);
long getxattr_impl(const char *path, const char *name, void *value, size_t size);
long lgetxattr_impl(const char *path, const char *name, void *value, size_t size);
long fgetxattr_impl(int fd, const char *name, void *value, size_t size);
long listxattr_impl(const char *path, char *list, size_t size);
long llistxattr_impl(const char *path, char *list, size_t size);
long flistxattr_impl(int fd, char *list, size_t size);
int removexattr_impl(const char *path, const char *name);
int lremovexattr_impl(const char *path, const char *name);
int fremovexattr_impl(int fd, const char *name);

#ifdef __cplusplus
}
#endif

#endif

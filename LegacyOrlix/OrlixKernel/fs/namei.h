#ifndef FS_NAMEI_H
#define FS_NAMEI_H

#include <linux/types.h>

#ifdef __cplusplus
extern "C" {
#endif

int renameat2_impl(int olddirfd, const char *oldpath, int newdirfd,
                   const char *newpath, unsigned int flags);
int chdir_impl(const char *path);
int fchdir_impl(int fd);
int getcwd_impl(char *buf, size_t size);
int mkdirat_impl(int dirfd, const char *pathname, mode_t mode);
int mkdir_impl(const char *pathname, mode_t mode);
int unlinkat_impl(int dirfd, const char *pathname, int flags);
int rmdir_impl(const char *pathname);
int unlink_impl(const char *pathname);
int linkat_impl(int olddirfd, const char *oldpath, int newdirfd,
                const char *newpath, int flags);
int link_impl(const char *oldpath, const char *newpath);
int symlinkat_impl(const char *target, int newdirfd, const char *linkpath);
int symlink_impl(const char *target, const char *linkpath);
ssize_t readlinkat_impl(int dirfd, const char *pathname, char *buf, size_t bufsiz);
ssize_t readlink_impl(const char *pathname, char *buf, size_t bufsiz);
int chroot_impl(const char *path);

#ifdef __cplusplus
}
#endif

#endif

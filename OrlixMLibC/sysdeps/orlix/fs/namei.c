#include <errno.h>
#include <stddef.h>
#include <sys/types.h>

#include <linux/fcntl.h>
#include <linux/types.h>

extern int chdir_impl(const char *path);
extern int fchdir_impl(int fd);
extern int getcwd_impl(char *buf, size_t size);
extern int mkdir_impl(const char *pathname, mode_t mode);
extern int mkdirat_impl(int dirfd, const char *pathname, mode_t mode);
extern int rmdir_impl(const char *pathname);
extern int unlink_impl(const char *pathname);
extern int unlinkat_impl(int dirfd, const char *pathname, int flags);
extern int link_impl(const char *oldpath, const char *newpath);
extern int linkat_impl(int olddirfd, const char *oldpath, int newdirfd, const char *newpath, int flags);
extern int symlink_impl(const char *target, const char *linkpath);
extern int symlinkat_impl(const char *target, int newdirfd, const char *linkpath);
extern __kernel_ssize_t readlink_impl(const char *pathname, char *buf, size_t bufsiz);
extern __kernel_ssize_t readlinkat_impl(int dirfd, const char *pathname, char *buf, size_t bufsiz);
extern int renameat2_impl(int olddirfd, const char *oldpath, int newdirfd, const char *newpath, unsigned int flags);
extern int chroot_impl(const char *path);

static int wrap_int_result(int ret) {
    if (ret < 0) {
        errno = -ret;
        return -1;
    }
    return ret;
}

static ssize_t wrap_ssize_result(__kernel_ssize_t ret) {
    if (ret < 0) {
        errno = (int)-ret;
        return -1;
    }
    return (ssize_t)ret;
}

__attribute__((visibility("default"))) int chdir(const char *path) {
    return wrap_int_result(chdir_impl(path));
}

__attribute__((visibility("default"))) int fchdir(int fd) {
    return wrap_int_result(fchdir_impl(fd));
}

__attribute__((visibility("default"))) char *getcwd(char *buf, size_t size) {
    int ret = getcwd_impl(buf, size);
    if (ret < 0) {
        errno = -ret;
        return NULL;
    }
    return buf;
}

__attribute__((visibility("default"))) int mkdir(const char *pathname, mode_t mode) {
    return wrap_int_result(mkdir_impl(pathname, mode));
}

__attribute__((visibility("default"))) int mkdirat(int dirfd, const char *pathname, mode_t mode) {
    return wrap_int_result(mkdirat_impl(dirfd, pathname, mode));
}

__attribute__((visibility("default"))) int rmdir(const char *pathname) {
    return wrap_int_result(rmdir_impl(pathname));
}

__attribute__((visibility("default"))) int unlink(const char *pathname) {
    return wrap_int_result(unlink_impl(pathname));
}

__attribute__((visibility("default"))) int unlinkat(int dirfd, const char *pathname, int flags) {
    return wrap_int_result(unlinkat_impl(dirfd, pathname, flags));
}

__attribute__((visibility("default"))) int link(const char *oldpath, const char *newpath) {
    return wrap_int_result(link_impl(oldpath, newpath));
}

__attribute__((visibility("default"))) int linkat(int olddirfd, const char *oldpath, int newdirfd, const char *newpath, int flags) {
    return wrap_int_result(linkat_impl(olddirfd, oldpath, newdirfd, newpath, flags));
}

__attribute__((visibility("default"))) int symlink(const char *target, const char *linkpath) {
    return wrap_int_result(symlink_impl(target, linkpath));
}

__attribute__((visibility("default"))) int symlinkat(const char *target, int newdirfd, const char *linkpath) {
    return wrap_int_result(symlinkat_impl(target, newdirfd, linkpath));
}

__attribute__((visibility("default"))) ssize_t readlink(const char *pathname, char *buf, size_t bufsiz) {
    return wrap_ssize_result(readlink_impl(pathname, buf, bufsiz));
}

__attribute__((visibility("default"))) ssize_t readlinkat(int dirfd, const char *pathname, char *buf, size_t bufsiz) {
    return wrap_ssize_result(readlinkat_impl(dirfd, pathname, buf, bufsiz));
}

__attribute__((visibility("default"))) int rename(const char *oldpath, const char *newpath) {
    return wrap_int_result(renameat2_impl(AT_FDCWD, oldpath, AT_FDCWD, newpath, 0));
}

__attribute__((visibility("default"))) int renameat(int olddirfd, const char *oldpath, int newdirfd, const char *newpath) {
    return wrap_int_result(renameat2_impl(olddirfd, oldpath, newdirfd, newpath, 0));
}

__attribute__((visibility("default"))) int renameat2(int olddirfd, const char *oldpath, int newdirfd, const char *newpath, unsigned int flags) {
    return wrap_int_result(renameat2_impl(olddirfd, oldpath, newdirfd, newpath, flags));
}

__attribute__((visibility("default"))) int chroot(const char *path) {
    return wrap_int_result(chroot_impl(path));
}

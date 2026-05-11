#include <errno.h>

#include <fcntl.h>
#include <sys/stat.h>

extern int stat_impl(const char *pathname, struct stat *statbuf);
extern int fstat_impl(int fd, struct stat *statbuf);
extern int lstat_impl(const char *pathname, struct stat *statbuf);
extern int access_impl(const char *pathname, int mode);
extern int faccessat_impl(int dirfd, const char *pathname, int mode, int flags);
extern int fstatat_impl(int dirfd, const char *pathname, struct stat *statbuf, int flags);
extern int statx_impl(int dirfd, const char *pathname, int flags, unsigned int mask,
                      struct statx *statxbuf);

static int wrap_int_result(int ret) {
    if (ret < 0) {
        errno = -ret;
        return -1;
    }
    return ret;
}

__attribute__((visibility("default"))) int stat(const char *pathname, struct stat *statbuf) {
    return wrap_int_result(stat_impl(pathname, statbuf));
}

__attribute__((visibility("default"))) int fstat(int fd, struct stat *statbuf) {
    return wrap_int_result(fstat_impl(fd, statbuf));
}

__attribute__((visibility("default"))) int lstat(const char *pathname, struct stat *statbuf) {
    return wrap_int_result(lstat_impl(pathname, statbuf));
}

__attribute__((visibility("default"))) int access(const char *pathname, int mode) {
    return wrap_int_result(access_impl(pathname, mode));
}

__attribute__((visibility("default"))) int faccessat(int dirfd, const char *pathname, int mode,
                                                     int flags) {
    return wrap_int_result(faccessat_impl(dirfd, pathname, mode, flags));
}

__attribute__((visibility("default"))) int fstatat(int dirfd, const char *pathname,
                                                   struct stat *statbuf, int flags) {
    return wrap_int_result(fstatat_impl(dirfd, pathname, statbuf, flags));
}

__attribute__((visibility("default"))) int newfstatat(int dirfd, const char *pathname,
                                                      struct stat *statbuf, int flags) {
    return wrap_int_result(fstatat_impl(dirfd, pathname, statbuf, flags));
}

__attribute__((visibility("default"))) int statx(int dirfd, const char *pathname, int flags,
                                                 unsigned int mask, struct statx *statxbuf) {
    return wrap_int_result(statx_impl(dirfd, pathname, flags, mask, statxbuf));
}

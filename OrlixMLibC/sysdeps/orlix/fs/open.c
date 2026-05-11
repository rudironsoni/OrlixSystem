#include <errno.h>
#include <stdarg.h>

#include <fcntl.h>
#include <sys/types.h>

#include <linux/types.h>

extern int open_impl(const char *pathname, int flags, __kernel_mode_t mode);
extern int openat_impl(int dirfd, const char *pathname, int flags, __kernel_mode_t mode);
extern int creat_impl(const char *pathname, __kernel_mode_t mode);
extern int close_impl(int fd);

static int host_result_from_kernel(int ret) {
    if (ret < 0) {
        errno = -ret;
        return -1;
    }
    return ret;
}

__attribute__((visibility("default"))) int open(const char *pathname, int flags, ...) {
    mode_t mode = 0;

    if ((flags & O_CREAT) != 0) {
        va_list args;
        va_start(args, flags);
        mode = va_arg(args, int);
        va_end(args);
    }

    return host_result_from_kernel(open_impl(pathname, flags, (__kernel_mode_t)mode));
}

__attribute__((visibility("default"))) int openat(int dirfd, const char *pathname, int flags, ...) {
    mode_t mode = 0;

    if ((flags & O_CREAT) != 0) {
        va_list args;
        va_start(args, flags);
        mode = va_arg(args, int);
        va_end(args);
    }

    return host_result_from_kernel(openat_impl(dirfd, pathname, flags, (__kernel_mode_t)mode));
}

__attribute__((visibility("default"))) int creat(const char *pathname, mode_t mode) {
    return host_result_from_kernel(creat_impl(pathname, (__kernel_mode_t)mode));
}

__attribute__((visibility("default"))) int close(int fd) {
    int ret = close_impl(fd);

    if (ret < 0) {
        return ret;
    }
    return 0;
}

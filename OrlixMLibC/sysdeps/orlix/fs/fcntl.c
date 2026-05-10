#include <errno.h>
#include <stdarg.h>

#include <linux/fcntl.h>

extern int dup_impl(int oldfd);
extern int dup2_impl(int oldfd, int newfd);
extern int dup3_impl(int oldfd, int newfd, int flags);
extern int fcntl_impl(int fd, int cmd, ...);
extern int flock_impl(int fd, int operation);

static int wrap_int_result(int ret) {
    if (ret < 0) {
        errno = -ret;
        return -1;
    }
    return ret;
}

__attribute__((visibility("default"))) int dup(int oldfd) {
    return wrap_int_result(dup_impl(oldfd));
}

__attribute__((visibility("default"))) int dup2(int oldfd, int newfd) {
    return wrap_int_result(dup2_impl(oldfd, newfd));
}

__attribute__((visibility("default"))) int dup3(int oldfd, int newfd, int flags) {
    return wrap_int_result(dup3_impl(oldfd, newfd, flags));
}

__attribute__((visibility("default"))) int flock(int fd, int operation) {
    return wrap_int_result(flock_impl(fd, operation));
}

__attribute__((visibility("default"))) int fcntl(int fd, int cmd, ...) {
    va_list args;
    int arg = 0;

    va_start(args, cmd);
    arg = va_arg(args, int);
    va_end(args);

    return wrap_int_result(fcntl_impl(fd, cmd, arg));
}

#include <asm/ioctls.h>
#include <asm-generic/termbits.h>
#include <errno.h>
#include <linux/types.h>
#include <stdarg.h>

extern int ioctl_impl(int fd, unsigned long request, void *arg);

static int host_int_result_from_kernel(int ret) {
    if (ret < 0) {
        errno = -ret;
        return -1;
    }
    return ret;
}

__attribute__((visibility("default"))) int ioctl(int fd, unsigned long request, ...) {
    va_list args;
    void *arg;

    va_start(args, request);
    arg = va_arg(args, void *);
    va_end(args);
    return host_int_result_from_kernel(ioctl_impl(fd, request, arg));
}

__attribute__((visibility("default"))) __kernel_pid_t tcgetpgrp(int fd) {
    __s32 pgrp = 0;
    int ret = ioctl_impl(fd, TIOCGPGRP, &pgrp);

    if (ret < 0) {
        errno = -ret;
        return -1;
    }

    return (__kernel_pid_t)pgrp;
}

__attribute__((visibility("default"))) int tcsetpgrp(int fd, __kernel_pid_t pgrp) {
    __s32 foreground_pgrp = (__s32)pgrp;

    return host_int_result_from_kernel(ioctl_impl(fd, TIOCSPGRP, &foreground_pgrp));
}

__attribute__((visibility("default"))) __kernel_pid_t tcgetsid(int fd) {
    __s32 sid = 0;
    int ret = ioctl_impl(fd, TIOCGSID, &sid);

    if (ret < 0) {
        errno = -ret;
        return -1;
    }

    return (__kernel_pid_t)sid;
}

__attribute__((visibility("default"))) int isatty(int fd) {
    struct termios termios;
    int ret = ioctl_impl(fd, TCGETS, &termios);

    if (ret < 0) {
        errno = -ret;
        return 0;
    }

    return 1;
}

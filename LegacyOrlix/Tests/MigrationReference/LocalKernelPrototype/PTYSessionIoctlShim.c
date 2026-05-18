#include <stdarg.h>
#include <sys/ioctl.h>

int pty_contract_ioctl(int fd, unsigned long request, ...) {
    va_list args;
    void *arg;

    va_start(args, request);
    arg = va_arg(args, void *);
    va_end(args);

    return ioctl(fd, request, arg);
}

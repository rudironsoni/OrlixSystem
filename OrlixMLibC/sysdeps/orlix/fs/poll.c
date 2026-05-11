#include <poll.h>
#include <sys/select.h>
#include <sys/time.h>

#include <linux/time_types.h>

extern int poll_impl(struct pollfd *fds, unsigned long nfds, int timeout);
extern int select_impl(int nfds,
                       void *readfds,
                       void *writefds,
                       void *errorfds,
                       struct __kernel_old_timeval *timeout);

__attribute__((visibility("default"))) int poll(struct pollfd *fds, nfds_t nfds, int timeout) {
    return poll_impl(fds, (unsigned long)nfds, timeout);
}

__attribute__((visibility("default"))) int select(int nfds,
                                                  fd_set *readfds,
                                                  fd_set *writefds,
                                                  fd_set *errorfds,
                                                  struct timeval *timeout) {
    struct __kernel_old_timeval kernel_timeout;
    struct __kernel_old_timeval *kernel_timeout_ptr = 0;

    if (timeout) {
        kernel_timeout.tv_sec = timeout->tv_sec;
        kernel_timeout.tv_usec = timeout->tv_usec;
        kernel_timeout_ptr = &kernel_timeout;
    }
    return select_impl(nfds, readfds, writefds, errorfds, kernel_timeout_ptr);
}

#include <errno.h>
#include <signal.h>

#include <linux/eventpoll.h>

#include "epoll_mask.h"

extern int epoll_create_impl(int size);
extern int epoll_create1_impl(int flags);
extern int epoll_ctl_impl(int epfd, int op, int fd, struct epoll_event *event);
extern int epoll_wait_impl(int epfd, struct epoll_event *events, int maxevents, int timeout);
extern int epoll_pwait_impl(int epfd, struct epoll_event *events, int maxevents, int timeout);

static int wrap_int_result(int ret) {
    if (ret < 0) {
        errno = -ret;
        return -1;
    }
    return ret;
}

__attribute__((visibility("default"))) int epoll_create(int size) {
    return wrap_int_result(epoll_create_impl(size));
}

__attribute__((visibility("default"))) int epoll_create1(int flags) {
    return wrap_int_result(epoll_create1_impl(flags));
}

__attribute__((visibility("default"))) int epoll_ctl(int epfd, int op, int fd,
                                                     struct epoll_event *event) {
    return wrap_int_result(epoll_ctl_impl(epfd, op, fd, event));
}

__attribute__((visibility("default"))) int epoll_wait(int epfd, struct epoll_event *events,
                                                      int maxevents, int timeout) {
    return wrap_int_result(epoll_wait_impl(epfd, events, maxevents, timeout));
}

__attribute__((visibility("default"))) int epoll_pwait(int epfd, struct epoll_event *events,
                                                       int maxevents, int timeout,
                                                       const sigset_t *sigmask) {
    epoll_sigmask_state_t state;
    int ret;

    epoll_sigmask_save(&state, sigmask);
    ret = epoll_pwait_impl(epfd, events, maxevents, timeout);
    epoll_sigmask_restore(&state);
    return wrap_int_result(ret);
}

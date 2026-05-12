#include <asm/unistd.h>
#include <uapi/linux/errno.h>
#include <uapi/linux/eventpoll.h>
#include <uapi/linux/poll.h>
#include <linux/net.h>

#include <stdint.h>

#include "EpollContract.h"
#include "runtime/syscall.h"

extern int errno;

extern int close_impl(int fd);
extern int epoll_create1_impl(int flags);
extern int epoll_ctl_impl(int epfd, int op, int fd, struct epoll_event *event);
extern int epoll_wait_impl(int epfd, struct epoll_event *events, int maxevents, int timeout);
extern long write_impl(int fd, const void *buf, size_t count);

static int close_if_open(int fd) {
    return fd >= 0 ? close_impl(fd) : 0;
}

static int add_pipe_read(int epfd, int fd, uint64_t data) {
    struct epoll_event event;

    event.events = EPOLLIN;
    event.data = data;
    return epoll_ctl_impl(epfd, EPOLL_CTL_ADD, fd, &event);
}

int epoll_contract_ctl_add_socketpair_read_end(void) {
    int epfd = -1;
    int fds[2] = {-1, -1};
    long sret;
    int ret = 0;

    epfd = epoll_create1_impl(0);
    if (epfd < 0) {
        return errno;
    }
    sret = syscall_dispatch_impl(__NR_socketpair, AF_UNIX, SOCK_STREAM, 0,
                                 (long)(uintptr_t)fds, 0, 0);
    if (sret != 0) {
        ret = sret < 0 ? (int)-sret : EIO;
        goto out;
    }
    if (add_pipe_read(epfd, fds[0], 11) != 0) {
        ret = errno ? errno : EIO;
    }

out:
    close_if_open(epfd);
    close_if_open(fds[0]);
    close_if_open(fds[1]);
    return ret;
}

int epoll_contract_wait_socketpair_readable_after_write(void) {
    int epfd = -1;
    int fds[2] = {-1, -1};
    int ret = 0;
    long sret;
    struct epoll_event event;

    epfd = epoll_create1_impl(0);
    if (epfd < 0) {
        return errno;
    }
    sret = syscall_dispatch_impl(__NR_socketpair, AF_UNIX, SOCK_STREAM, 0,
                                 (long)(uintptr_t)fds, 0, 0);
    if (sret != 0) {
        ret = sret < 0 ? (int)-sret : EIO;
        goto out;
    }
    if (add_pipe_read(epfd, fds[0], 99) != 0 || write_impl(fds[1], "x", 1) != 1) {
        ret = errno ? errno : EIO;
        goto out;
    }
    if (epoll_wait_impl(epfd, &event, 1, 0) != 1 ||
        (event.events & EPOLLIN) == 0 ||
        event.data != 99) {
        ret = errno ? errno : EIO;
    }

out:
    close_if_open(epfd);
    close_if_open(fds[0]);
    close_if_open(fds[1]);
    return ret;
}

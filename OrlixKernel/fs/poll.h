#ifndef FS_POLL_H
#define FS_POLL_H

#include <linux/poll.h>
#include <linux/time.h>
#include <linux/types.h>

#ifdef __cplusplus
extern "C" {
#endif

int poll_impl(struct pollfd *fds, __kernel_ulong_t nfds, int timeout);
int select_impl(int nfds,
                __kernel_fd_set *readfds,
                __kernel_fd_set *writefds,
                __kernel_fd_set *errorfds,
                struct __kernel_old_timeval *timeout);

#ifdef __cplusplus
}
#endif

#endif

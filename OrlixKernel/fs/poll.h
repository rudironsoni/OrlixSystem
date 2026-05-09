#ifndef FS_POLL_H
#define FS_POLL_H

#include <stdint.h>

#include <linux/poll.h>
#include <linux/types.h>

#include "internal/private/kernel_select_compat.h"
#include "internal/private/kernel_time_compat.h"

#ifdef __cplusplus
extern "C" {
#endif

int poll_impl(struct pollfd *fds, __kernel_ulong_t nfds, int timeout);
int select_impl(int nfds, fd_set *readfds, fd_set *writefds, fd_set *errorfds,
                struct kernel_timeval *timeout);
void poll_notify_readiness_impl(void);
short poll_fd_revents_impl(int fd, short events, int *is_virtual);
int poll_wait_for_readiness_impl(int timeout);

#ifdef __cplusplus
}
#endif

#endif

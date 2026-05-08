#ifndef FS_POLL_H
#define FS_POLL_H

#include <stdint.h>

#define poll ixland_host_poll_frame
#include <sys/select.h>
#include <sys/time.h>
#undef poll

#include <linux/poll.h>

#include "internal/private/kernel_type_compat.h"

#ifdef __cplusplus
extern "C" {
#endif

int poll_impl(struct pollfd *fds, nfds_t nfds, int timeout);
int select_impl(int nfds, fd_set *readfds, fd_set *writefds, fd_set *errorfds, struct timeval *timeout);
void poll_notify_readiness_impl(void);
short poll_fd_revents_impl(int fd, short events, int *is_virtual);
int poll_wait_for_readiness_impl(int timeout);

#ifdef __cplusplus
}
#endif

#endif

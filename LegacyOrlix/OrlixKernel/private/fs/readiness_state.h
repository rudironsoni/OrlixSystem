#ifndef PRIVATE_FS_READINESS_STATE_H
#define PRIVATE_FS_READINESS_STATE_H

void poll_notify_readiness_impl(void);
short poll_fd_revents_impl(int fd, short events, int *is_virtual);
int poll_wait_for_readiness_impl(int timeout);

#endif /* PRIVATE_FS_READINESS_STATE_H */

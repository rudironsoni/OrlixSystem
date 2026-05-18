#ifndef INTERNAL_FS_POLL_H
#define INTERNAL_FS_POLL_H

struct pollfd;

int backing_poll(struct pollfd *fds, unsigned int nfds, int timeout);

#endif

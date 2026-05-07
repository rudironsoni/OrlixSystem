#ifndef IXLAND_INTERNAL_PRIVATE_BACKING_POLL_H
#define IXLAND_INTERNAL_PRIVATE_BACKING_POLL_H

struct pollfd;

int backing_poll(struct pollfd *fds, unsigned int nfds, int timeout);

#endif

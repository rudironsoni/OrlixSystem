#ifndef PRIVATE_FS_EVENTPOLL_STATE_H
#define PRIVATE_FS_EVENTPOLL_STATE_H

#include <linux/types.h>

struct epoll_instance;

int epoll_fdinfo_content_impl(struct epoll_instance *instance, char *buf, size_t buf_len,
                              size_t *pos);
void epoll_release_fd_impl(struct epoll_instance *instance);

#endif /* PRIVATE_FS_EVENTPOLL_STATE_H */

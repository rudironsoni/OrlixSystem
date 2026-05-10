#ifndef FS_EVENTPOLL_H
#define FS_EVENTPOLL_H

#include <stddef.h>

#include <linux/fcntl.h>
#ifndef _LINUX_FCNTL_H
#define _LINUX_FCNTL_H
#endif
#include <linux/eventpoll.h>

#ifdef __cplusplus
extern "C" {
#endif

struct epoll_instance;

int epoll_create_impl(int size);
int epoll_create1_impl(int flags);
int epoll_ctl_impl(int epfd, int op, int fd, struct epoll_event *event);
int epoll_wait_impl(int epfd, struct epoll_event *events, int maxevents, int timeout);
int epoll_pwait_impl(int epfd, struct epoll_event *events, int maxevents, int timeout);
int epoll_fdinfo_content_impl(struct epoll_instance *instance, char *buf, size_t buf_len, size_t *pos);
void epoll_release_fd_impl(struct epoll_instance *instance);

#ifdef __cplusplus
}
#endif

#endif

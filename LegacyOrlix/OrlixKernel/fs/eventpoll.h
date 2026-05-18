#ifndef FS_EVENTPOLL_H
#define FS_EVENTPOLL_H

#include <linux/types.h>

#include <linux/fcntl.h>
/*
 * Keep the epoll upstream Linux ABI contract off the deep kernel fcntl owner graph in this
 * translation unit family.
 */
#define _LINUX_FCNTL_H
#include <linux/eventpoll.h>
#undef _LINUX_FCNTL_H

#ifdef __cplusplus
extern "C" {
#endif

struct epoll_instance;

int epoll_create_impl(int size);
int epoll_create1_impl(int flags);
int epoll_ctl_impl(int epfd, int op, int fd, struct epoll_event *event);
int epoll_wait_impl(int epfd, struct epoll_event *events, int maxevents, int timeout);
int epoll_pwait_impl(int epfd, struct epoll_event *events, int maxevents, int timeout);

#ifdef __cplusplus
}
#endif

#endif

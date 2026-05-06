#include "eventpoll.h"

#include <linux/fcntl.h>

#include <errno.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fdtable.h"
#include "fs_sync.h"
#include "poll.h"
#include "../kernel/task.h"

typedef struct epitem {
    int fd;
    struct epoll_event event;
    __poll_t last_ready_events;
    bool oneshot_disabled;
    struct epitem *next;
} epitem_t;

struct epoll_instance {
    atomic_int refs;
    fs_mutex_t lock;
    epitem_t *items;
};

static void epoll_retain(struct epoll_instance *instance) {
    if (instance) {
        atomic_fetch_add(&instance->refs, 1);
    }
}

void epoll_release_fd_impl(struct epoll_instance *instance) {
    if (!instance) {
        return;
    }
    if (atomic_fetch_sub(&instance->refs, 1) == 1) {
        epitem_t *item = instance->items;
        while (item) {
            epitem_t *next = item->next;
            free(item);
            item = next;
        }
        fs_mutex_destroy(&instance->lock);
        free(instance);
    }
}

static struct epoll_instance *epoll_get_from_fd(int epfd) {
    fd_entry_t *entry;
    struct epoll_instance *instance;

    entry = get_fd_entry_impl(epfd);
    if (!entry) {
        errno = EBADF;
        return NULL;
    }
    if (!get_fd_is_epoll_impl(entry)) {
        put_fd_entry_impl(entry);
        errno = EINVAL;
        return NULL;
    }
    instance = get_fd_epoll_instance_impl(entry);
    epoll_retain(instance);
    put_fd_entry_impl(entry);
    return instance;
}

static epitem_t *epoll_find_item(struct epoll_instance *instance, int fd) {
    epitem_t *item = instance->items;
    while (item) {
        if (item->fd == fd) {
            return item;
        }
        item = item->next;
    }
    return NULL;
}

static __poll_t epoll_mask_requested_events(__poll_t events) {
    /* EPOLLET/EPOLLONESHOT are behavior modifiers, not readiness bits. */
    return events & ~((__poll_t)EPOLLET | (__poll_t)EPOLLONESHOT);
}

static int epoll_copy_ready(struct epoll_instance *instance, struct epoll_event *events, int maxevents) {
    int ready = 0;

    fs_mutex_lock(&instance->lock);
    for (epitem_t *item = instance->items; item && ready < maxevents; item = item->next) {
        if (item->oneshot_disabled) {
            continue;
        }
        int is_virtual = 0;
        __poll_t requested = epoll_mask_requested_events(item->event.events);
        short revents = poll_fd_revents_impl(item->fd, (short)requested, &is_virtual);
        if (revents == 0) {
            /* Reset edge state once readiness clears so next transition can be observed. */
            item->last_ready_events = 0;
            continue;
        }

        __poll_t current = (__poll_t)revents;
        if ((item->event.events & EPOLLET) != 0) {
            __poll_t newly_ready = current & ~item->last_ready_events;
            item->last_ready_events = current;
            if (newly_ready == 0) {
                continue;
            }
            current = newly_ready;
        } else {
            item->last_ready_events = current;
        }

        if ((item->event.events & EPOLLONESHOT) != 0) {
            item->oneshot_disabled = true;
        }

        events[ready].events = current;
        events[ready].data = item->event.data;
        ready++;
    }
    fs_mutex_unlock(&instance->lock);

    return ready;
}

int epoll_fdinfo_content_impl(struct epoll_instance *instance, char *buf, size_t buf_len, size_t *pos) {
    int ret = 0;

    if (!instance || !buf || !pos || *pos >= buf_len) {
        return -EINVAL;
    }

    fs_mutex_lock(&instance->lock);
    for (epitem_t *item = instance->items; item; item = item->next) {
        int written = snprintf(buf + *pos, buf_len - *pos,
                               "tfd:\t%d events:\t%08x data:\t%llx\n",
                               item->fd,
                               (unsigned int)item->event.events,
                               (unsigned long long)item->event.data);
        if (written < 0) {
            ret = -EINVAL;
            break;
        }
        if ((size_t)written >= buf_len - *pos) {
            *pos = buf_len - 1;
            ret = -ENOSPC;
            break;
        }
        *pos += (size_t)written;
    }
    fs_mutex_unlock(&instance->lock);
    return ret;
}

int epoll_create1_impl(int flags) {
    struct epoll_instance *instance;
    int fd;

    if (flags & ~EPOLL_CLOEXEC) {
        errno = EINVAL;
        return -1;
    }

    instance = calloc(1, sizeof(*instance));
    if (!instance) {
        errno = ENOMEM;
        return -1;
    }
    atomic_init(&instance->refs, 1);
    fs_mutex_init(&instance->lock);

    fd = alloc_fd_impl();
    if (fd < 0) {
        epoll_release_fd_impl(instance);
        return -1;
    }

    if (init_epoll_fd_entry_impl(fd, flags, instance) != 0) {
        free_fd_impl(fd);
        epoll_release_fd_impl(instance);
        return -1;
    }

    return fd;
}

int epoll_create_impl(int size) {
    if (size <= 0) {
        errno = EINVAL;
        return -1;
    }
    return epoll_create1_impl(0);
}

int epoll_ctl_impl(int epfd, int op, int fd, struct epoll_event *event) {
    struct epoll_instance *instance;
    epitem_t *item;

    if (fd < 0 || fd >= NR_OPEN_DEFAULT || !fdtable_is_used_impl(fd)) {
        errno = EBADF;
        return -1;
    }
    if (fd == epfd) {
        errno = EINVAL;
        return -1;
    }

    instance = epoll_get_from_fd(epfd);
    if (!instance) {
        return -1;
    }

    fs_mutex_lock(&instance->lock);
    item = epoll_find_item(instance, fd);

    switch (op) {
    case EPOLL_CTL_ADD:
        if (!event) {
            fs_mutex_unlock(&instance->lock);
            epoll_release_fd_impl(instance);
            errno = EFAULT;
            return -1;
        }
        if (item) {
            fs_mutex_unlock(&instance->lock);
            epoll_release_fd_impl(instance);
            errno = EEXIST;
            return -1;
        }
        item = calloc(1, sizeof(*item));
        if (!item) {
            fs_mutex_unlock(&instance->lock);
            epoll_release_fd_impl(instance);
            errno = ENOMEM;
            return -1;
        }
        item->fd = fd;
        item->event = *event;
        item->last_ready_events = 0;
        item->oneshot_disabled = false;
        item->next = instance->items;
        instance->items = item;
        break;
    case EPOLL_CTL_MOD:
        if (!event) {
            fs_mutex_unlock(&instance->lock);
            epoll_release_fd_impl(instance);
            errno = EFAULT;
            return -1;
        }
        if (!item) {
            fs_mutex_unlock(&instance->lock);
            epoll_release_fd_impl(instance);
            errno = ENOENT;
            return -1;
        }
        item->event = *event;
        item->last_ready_events = 0;
        item->oneshot_disabled = false;
        break;
    case EPOLL_CTL_DEL: {
        epitem_t **cursor = &instance->items;
        if (!item) {
            fs_mutex_unlock(&instance->lock);
            epoll_release_fd_impl(instance);
            errno = ENOENT;
            return -1;
        }
        while (*cursor && *cursor != item) {
            cursor = &(*cursor)->next;
        }
        if (*cursor) {
            *cursor = item->next;
        }
        free(item);
        break;
    }
    default:
        fs_mutex_unlock(&instance->lock);
        epoll_release_fd_impl(instance);
        errno = EINVAL;
        return -1;
    }

    fs_mutex_unlock(&instance->lock);
    epoll_release_fd_impl(instance);
    return 0;
}

int epoll_wait_impl(int epfd, struct epoll_event *events, int maxevents, int timeout) {
    return epoll_pwait_impl(epfd, events, maxevents, timeout);
}

int epoll_pwait_impl(int epfd, struct epoll_event *events, int maxevents, int timeout) {
    struct epoll_instance *instance;
    int remaining = timeout;

    if (!events || maxevents <= 0) {
        errno = EINVAL;
        return -1;
    }

    instance = epoll_get_from_fd(epfd);
    if (!instance) {
        return -1;
    }

    for (;;) {
        int ready = epoll_copy_ready(instance, events, maxevents);
        if (ready > 0 || timeout == 0) {
            epoll_release_fd_impl(instance);
            return ready;
        }

        int wait_ms = timeout < 0 ? -1 : remaining;
        if (timeout > 0 && remaining <= 0) {
            epoll_release_fd_impl(instance);
            return 0;
        }
        if (poll_wait_for_readiness_impl(wait_ms) < 0) {
            if (errno == EINTR) {
                task_restart_record_impl(get_current(), TASK_RESTART_EPOLL_WAIT,
                                         (uint64_t)(int64_t)epfd,
                                         (uint64_t)(uintptr_t)events,
                                         (uint64_t)(int64_t)maxevents,
                                         (uint64_t)(int64_t)timeout,
                                         0, 0);
            }
            epoll_release_fd_impl(instance);
            return -1;
        }
        if (timeout > 0) {
            remaining -= wait_ms;
        }
    }
}

__attribute__((visibility("default"))) int epoll_create(int size) {
    return epoll_create_impl(size);
}

__attribute__((visibility("default"))) int epoll_create1(int flags) {
    return epoll_create1_impl(flags);
}

__attribute__((visibility("default"))) int epoll_ctl(int epfd, int op, int fd, struct epoll_event *event) {
    return epoll_ctl_impl(epfd, op, fd, event);
}

__attribute__((visibility("default"))) int epoll_wait(int epfd, struct epoll_event *events, int maxevents, int timeout) {
    return epoll_wait_impl(epfd, events, maxevents, timeout);
}

__attribute__((visibility("default"))) int epoll_pwait(int epfd, struct epoll_event *events, int maxevents, int timeout, const void *sigmask) {
    if (sigmask) {
        errno = ENOSYS;
        return -1;
    }
    return epoll_pwait_impl(epfd, events, maxevents, timeout);
}

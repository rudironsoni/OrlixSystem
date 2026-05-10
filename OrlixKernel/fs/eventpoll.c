#include "eventpoll.h"

#include <uapi/linux/errno.h>
#include <uapi/linux/fcntl.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

#include "fdtable.h"
#include "internal/slab.h"
#include "internal/fs/lock.h"
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
            kfree(item);
            item = next;
        }
        fs_mutex_destroy(&instance->lock);
        kfree(instance);
    }
}

static int epoll_get_from_fd(int epfd, struct epoll_instance **instance_out) {
    fd_entry_t *entry;
    struct epoll_instance *instance;

    if (!instance_out) {
        return -EINVAL;
    }
    *instance_out = NULL;

    entry = get_fd_entry_impl(epfd);
    if (!entry) {
        return -EBADF;
    }
    if (!get_fd_is_epoll_impl(entry)) {
        put_fd_entry_impl(entry);
        return -EINVAL;
    }
    instance = get_fd_epoll_instance_impl(entry);
    epoll_retain(instance);
    put_fd_entry_impl(entry);
    *instance_out = instance;
    return 0;
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
        int written = scnprintf(buf + *pos, buf_len - *pos,
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
    int ret;

    if (flags & ~EPOLL_CLOEXEC) {
        return -EINVAL;
    }

    instance = __kmalloc_noprof(sizeof(*instance), GFP_KERNEL | __GFP_ZERO);
    if (!instance) {
        return -ENOMEM;
    }
    atomic_init(&instance->refs, 1);
    fs_mutex_init(&instance->lock);

    fd = alloc_fd_impl();
    if (fd < 0) {
        epoll_release_fd_impl(instance);
        return fd;
    }

    ret = init_epoll_fd_entry_impl(fd, flags, instance);
    if (ret != 0) {
        free_fd_impl(fd);
        epoll_release_fd_impl(instance);
        return ret == -1 ? -ENOMEM : ret;
    }

    return fd;
}

int epoll_create_impl(int size) {
    if (size <= 0) {
        return -EINVAL;
    }
    return epoll_create1_impl(0);
}

int epoll_ctl_impl(int epfd, int op, int fd, struct epoll_event *event) {
    struct epoll_instance *instance;
    epitem_t *item;
    int ret;

    if (fd < 0 || fd >= NR_OPEN_DEFAULT || !fdtable_is_used_impl(fd)) {
        return -EBADF;
    }
    if (fd == epfd) {
        return -EINVAL;
    }

    ret = epoll_get_from_fd(epfd, &instance);
    if (ret < 0) {
        return ret;
    }

    fs_mutex_lock(&instance->lock);
    item = epoll_find_item(instance, fd);

    switch (op) {
    case EPOLL_CTL_ADD:
        if (!event) {
            fs_mutex_unlock(&instance->lock);
            epoll_release_fd_impl(instance);
            return -EFAULT;
        }
        if (item) {
            fs_mutex_unlock(&instance->lock);
            epoll_release_fd_impl(instance);
            return -EEXIST;
        }
        item = __kmalloc_noprof(sizeof(*item), GFP_KERNEL | __GFP_ZERO);
        if (!item) {
            fs_mutex_unlock(&instance->lock);
            epoll_release_fd_impl(instance);
            return -ENOMEM;
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
            return -EFAULT;
        }
        if (!item) {
            fs_mutex_unlock(&instance->lock);
            epoll_release_fd_impl(instance);
            return -ENOENT;
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
            return -ENOENT;
        }
        while (*cursor && *cursor != item) {
            cursor = &(*cursor)->next;
        }
        if (*cursor) {
            *cursor = item->next;
        }
        kfree(item);
        break;
    }
    default:
        fs_mutex_unlock(&instance->lock);
        epoll_release_fd_impl(instance);
        return -EINVAL;
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
    int ret;

    if (!events || maxevents <= 0) {
        return -EINVAL;
    }

    ret = epoll_get_from_fd(epfd, &instance);
    if (ret < 0) {
        return ret;
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
        ret = poll_wait_for_readiness_impl(wait_ms);
        if (ret < 0) {
            if (ret == -EINTR) {
                task_restart_record_impl(get_current(), TASK_RESTART_EPOLL_WAIT,
                                         (uint64_t)(int64_t)epfd,
                                         (uint64_t)(uintptr_t)events,
                                         (uint64_t)(int64_t)maxevents,
                                         (uint64_t)(int64_t)timeout,
                                         0, 0);
            }
            epoll_release_fd_impl(instance);
            return ret;
        }
        if (timeout > 0) {
            remaining -= wait_ms;
        }
    }
}

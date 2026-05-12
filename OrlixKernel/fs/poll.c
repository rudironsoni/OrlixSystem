#include "poll.h"

#include <linux/errno.h>
#include <linux/types.h>

#include <limits.h>

#include "fdtable.h"
#include "internal/fs/poll.h"
#include "pipe.h"
#include "pty.h"
#include "kernel/net/socket.h"
#include "../private/kernel/wait_queue_state.h"
#include "../kernel/wait_queue.h"
#include "../kernel/task.h"

extern int *__error(void);
extern void *calloc(size_t, size_t);
extern void free(void *);
extern void *memset(void *, int, size_t);

#define errno (*__error())

#define POLL_HOST_SLICE_MS 25

static struct wait_queue_head readiness_wait;
static int readiness_wait_initialized;
static uint64_t readiness_generation;

static void readiness_wait_init_once(void) {
    if (!readiness_wait_initialized) {
        wait_queue_init(&readiness_wait);
        readiness_generation = 0;
        readiness_wait_initialized = 1;
    }
}

static int backing_poll_wait(struct pollfd *fds, __kernel_ulong_t nfds, int timeout) {
    if (nfds > (__kernel_ulong_t)UINT_MAX) {
        errno = EINVAL;
        return -1;
    }
    return backing_poll(fds, (unsigned int)nfds, timeout);
}

void poll_notify_readiness_impl(void) {
    readiness_wait_init_once();
    wait_queue_lock(&readiness_wait);
    readiness_generation++;
    wait_queue_wake_all_locked(&readiness_wait);
    wait_queue_unlock(&readiness_wait);
}

static uint64_t poll_generation_snapshot(void) {
    uint64_t generation;

    readiness_wait_init_once();
    wait_queue_lock(&readiness_wait);
    generation = readiness_generation;
    wait_queue_unlock(&readiness_wait);
    return generation;
}

static bool synthetic_fd_read_ready(fd_entry_t *entry) {
    return get_fd_is_synthetic_proc_file_impl(entry) ||
           get_fd_is_synthetic_dir_impl(entry) ||
           get_fd_is_synthetic_dev_impl(entry);
}

static bool synthetic_fd_write_ready(fd_entry_t *entry) {
    return get_fd_is_synthetic_proc_file_impl(entry) ||
           get_fd_is_synthetic_dev_impl(entry);
}

short poll_fd_revents_impl(int fd, short events, int *is_virtual) {
    fd_entry_t *entry;
    short revents = 0;

    if (is_virtual) {
        *is_virtual = 0;
    }

    if (fd < 0) {
        return 0;
    }
    if (fd >= NR_OPEN_DEFAULT) {
        return POLLNVAL;
    }

    entry = get_fd_entry_impl(fd);
    if (!entry) {
        return POLLNVAL;
    }

    if (get_fd_is_pipe_impl(entry)) {
        struct pipe_endpoint *endpoint = get_fd_pipe_endpoint_impl(entry);
        if (is_virtual) {
            *is_virtual = 1;
        }
        put_fd_entry_impl(entry);
        return pipe_poll_revents_impl(endpoint, events);
    }

    if (get_fd_is_socket_impl(entry)) {
        struct socket_state *sock = get_fd_socket_impl(entry);
        if (is_virtual) {
            *is_virtual = 1;
        }
        put_fd_entry_impl(entry);
        return socket_poll_revents_impl(sock, events);
    }

    if (get_fd_is_eventfd_impl(entry)) {
        if (is_virtual) {
            *is_virtual = 1;
        }
        if ((events & (POLLIN | POLLRDNORM)) && eventfd_read_ready_entry_impl(entry)) {
            revents |= events & (POLLIN | POLLRDNORM);
        }
        if ((events & (POLLOUT | POLLWRNORM)) && eventfd_write_ready_entry_impl(entry)) {
            revents |= events & (POLLOUT | POLLWRNORM);
        }
        put_fd_entry_impl(entry);
        return revents;
    }

    if (get_fd_is_timerfd_impl(entry)) {
        if (is_virtual) {
            *is_virtual = 1;
        }
        if ((events & (POLLIN | POLLRDNORM)) && timerfd_read_ready_entry_impl(entry)) {
            revents |= events & (POLLIN | POLLRDNORM);
        }
        put_fd_entry_impl(entry);
        return revents;
    }

    if (get_fd_is_pidfd_impl(entry)) {
        if (is_virtual) {
            *is_virtual = 1;
        }
        if ((events & (POLLIN | POLLRDNORM)) && pidfd_read_ready_entry_impl(entry)) {
            revents |= events & (POLLIN | POLLRDNORM);
        }
        put_fd_entry_impl(entry);
        return revents;
    }

    if (get_fd_is_synthetic_pty_impl(entry)) {
        unsigned int pty_index = get_fd_synthetic_pty_index_impl(entry);
        bool is_master = get_fd_is_synthetic_pty_master_impl(entry);
        if (is_virtual) {
            *is_virtual = 1;
        }
        put_fd_entry_impl(entry);
        return pty_poll_revents_impl(pty_index, is_master, events);
    }

    if (get_fd_is_synthetic_proc_file_impl(entry) ||
        get_fd_is_synthetic_dir_impl(entry) ||
        get_fd_is_synthetic_dev_impl(entry)) {
        if (is_virtual) {
            *is_virtual = 1;
        }
        if ((events & (POLLIN | POLLRDNORM)) && synthetic_fd_read_ready(entry)) {
            revents |= events & (POLLIN | POLLRDNORM);
        }
        if ((events & (POLLOUT | POLLWRNORM)) && synthetic_fd_write_ready(entry)) {
            revents |= events & (POLLOUT | POLLWRNORM);
        }
        put_fd_entry_impl(entry);
        return revents;
    }

    put_fd_entry_impl(entry);
    if (is_virtual) {
        *is_virtual = 0;
    }
    return 0;
}

int poll_wait_for_readiness_impl(int timeout) {
    uint64_t observed;
    int ret;

    readiness_wait_init_once();
    observed = poll_generation_snapshot();

    wait_queue_lock(&readiness_wait);
    if (readiness_generation != observed) {
        wait_queue_unlock(&readiness_wait);
        return 0;
    }

    if (timeout < 0) {
        ret = wait_queue_wait_locked_interruptible(&readiness_wait);
    } else {
        ret = wait_queue_wait_locked_interruptible_timeout(&readiness_wait, timeout);
    }
    wait_queue_unlock(&readiness_wait);

    if (ret == -ETIMEDOUT) {
        return 0;
    }
    return ret;
}

static int poll_wait_after_snapshot(uint64_t observed_generation, int timeout) {
    int ret;

    readiness_wait_init_once();
    wait_queue_lock(&readiness_wait);
    if (readiness_generation != observed_generation) {
        wait_queue_unlock(&readiness_wait);
        return 0;
    }

    if (timeout < 0) {
        ret = wait_queue_wait_locked_interruptible(&readiness_wait);
    } else {
        ret = wait_queue_wait_locked_interruptible_timeout(&readiness_wait, timeout);
    }
    wait_queue_unlock(&readiness_wait);

    if (ret == -ETIMEDOUT) {
        return 0;
    }
    return ret;
}

static int poll_snapshot(struct pollfd *fds, __kernel_ulong_t nfds, bool *has_virtual_out, bool *has_backing_out) {
    int ready_count = 0;
    int backing_fds_count = 0;
    bool has_virtual = false;
    struct pollfd *backing_fds = NULL;
    int *fd_map = NULL;

    if (has_virtual_out) {
        *has_virtual_out = false;
    }
    if (has_backing_out) {
        *has_backing_out = false;
    }

    if (nfds > 0) {
        backing_fds = calloc(nfds, sizeof(struct pollfd));
        fd_map = calloc(nfds, sizeof(int));
        if (!backing_fds || !fd_map) {
            free(backing_fds);
            free(fd_map);
            errno = ENOMEM;
            return -1;
        }
    }

    for (__kernel_ulong_t i = 0; i < nfds; i++) {
        int is_virtual = 0;
        short revents;

        fds[i].revents = 0;
        if (fds[i].fd < 0) {
            continue;
        }

        revents = poll_fd_revents_impl(fds[i].fd, fds[i].events, &is_virtual);
        if (is_virtual || revents == POLLNVAL) {
            fds[i].revents = revents;
            if (revents != 0) {
                ready_count++;
            }
            if (is_virtual) {
                has_virtual = true;
            }
            continue;
        }

        backing_fds[backing_fds_count].fd = fds[i].fd;
        backing_fds[backing_fds_count].events = fds[i].events;
        backing_fds[backing_fds_count].revents = 0;
        fd_map[backing_fds_count] = (int)i;
        backing_fds_count++;
    }

    if (backing_fds_count > 0) {
        int backing_ready = backing_poll_wait(backing_fds, (__kernel_ulong_t)backing_fds_count, 0);
        if (backing_ready < 0) {
            free(backing_fds);
            free(fd_map);
            return -1;
        }

        for (int i = 0; i < backing_fds_count; i++) {
            int orig_idx = fd_map[i];
            fds[orig_idx].revents = backing_fds[i].revents;
            if (backing_fds[i].revents != 0) {
                ready_count++;
            }
        }
    }

    if (has_virtual_out) {
        *has_virtual_out = has_virtual;
    }
    if (has_backing_out) {
        *has_backing_out = backing_fds_count > 0;
    }

    free(backing_fds);
    free(fd_map);
    return ready_count;
}

static int poll_impl_common(struct pollfd *fds, __kernel_ulong_t nfds, int timeout, bool record_restart) {
    int remaining = timeout;

    if (!fds && nfds > 0) {
        errno = EFAULT;
        return -1;
    }

    if (nfds == 0) {
        if (timeout == 0) {
            return 0;
        }
        int ret = poll_wait_for_readiness_impl(timeout);
        if (ret == -EINTR) {
            if (record_restart) {
                task_restart_record_impl(task_current(), TASK_RESTART_POLL,
                                         (uint64_t)(uintptr_t)fds, (uint64_t)nfds,
                                         (uint64_t)(int64_t)timeout, 0, 0, 0);
            }
        }
        return ret;
    }

    for (;;) {
        bool has_virtual = false;
        bool has_backing = false;
        uint64_t observed_generation = poll_generation_snapshot();
        int ready_count = poll_snapshot(fds, nfds, &has_virtual, &has_backing);
        if (ready_count < 0) {
            return -1;
        }
        if (ready_count > 0 || timeout == 0) {
            return ready_count;
        }

        if (!has_virtual && has_backing) {
            return backing_poll_wait(fds, nfds, timeout);
        }

        int wait_ms = timeout < 0 ? -1 : remaining;
        if (timeout > 0 && remaining <= 0) {
            return 0;
        }
        if (has_backing && (wait_ms < 0 || wait_ms > POLL_HOST_SLICE_MS)) {
            wait_ms = POLL_HOST_SLICE_MS;
        }

        {
            int ret = poll_wait_after_snapshot(observed_generation, wait_ms);
            if (ret < 0) {
                if (ret == -EINTR && record_restart) {
                    task_restart_record_impl(task_current(), TASK_RESTART_POLL,
                                             (uint64_t)(uintptr_t)fds, (uint64_t)nfds,
                                             (uint64_t)(int64_t)timeout, 0, 0, 0);
                }
                return ret;
            }
        }
        if (timeout > 0) {
            remaining -= wait_ms;
        }
    }
}

int poll_impl(struct pollfd *fds, __kernel_ulong_t nfds, int timeout) {
    return poll_impl_common(fds, nfds, timeout, true);
}

static int timeval_to_timeout_ms(const struct __kernel_old_timeval *timeout) {
    if (!timeout) {
        return -1;
    }
    if (timeout->tv_sec < 0 || timeout->tv_usec < 0) {
        errno = EINVAL;
        return -2;
    }

    uint64_t sec_ms = (uint64_t)timeout->tv_sec * 1000ULL;
    uint64_t usec_ms = ((uint64_t)timeout->tv_usec + 999ULL) / 1000ULL;
    uint64_t total = sec_ms + usec_ms;
    if (total > (uint64_t)INT_MAX) {
        total = (uint64_t)INT_MAX;
    }
    return (int)total;
}

static void fdset_zero(__kernel_fd_set *set) {
    if (!set) {
        return;
    }
    memset(set->fds_bits, 0, sizeof(set->fds_bits));
}

static bool fdset_isset(int fd, const __kernel_fd_set *set) {
    unsigned int bits_per_word = (unsigned int)(8U * sizeof(set->fds_bits[0]));
    unsigned int word;
    unsigned int bit;

    if (!set || fd < 0 || fd >= __FD_SETSIZE) {
        return false;
    }
    word = (unsigned int)fd / bits_per_word;
    bit = (unsigned int)fd % bits_per_word;
    return (set->fds_bits[word] & (1UL << bit)) != 0;
}

static void fdset_set(int fd, __kernel_fd_set *set) {
    unsigned int bits_per_word = (unsigned int)(8U * sizeof(set->fds_bits[0]));
    unsigned int word;
    unsigned int bit;

    if (!set || fd < 0 || fd >= __FD_SETSIZE) {
        return;
    }
    word = (unsigned int)fd / bits_per_word;
    bit = (unsigned int)fd % bits_per_word;
    set->fds_bits[word] |= (1UL << bit);
}

int select_impl(int nfds,
                __kernel_fd_set *readfds,
                __kernel_fd_set *writefds,
                __kernel_fd_set *errorfds,
                struct __kernel_old_timeval *timeout) {
    __kernel_fd_set requested_read;
    __kernel_fd_set requested_write;
    __kernel_fd_set requested_error;
    __kernel_fd_set *requested_read_ptr = NULL;
    __kernel_fd_set *requested_write_ptr = NULL;
    __kernel_fd_set *requested_error_ptr = NULL;
    struct pollfd *pfds;
    int requested = 0;
    int timeout_ms;

    if (nfds < 0) {
        errno = EINVAL;
        return -1;
    }

    fdset_zero(&requested_read);
    fdset_zero(&requested_write);
    fdset_zero(&requested_error);

    if (readfds) {
        requested_read = *readfds;
        requested_read_ptr = &requested_read;
        fdset_zero(readfds);
    }
    if (writefds) {
        requested_write = *writefds;
        requested_write_ptr = &requested_write;
        fdset_zero(writefds);
    }
    if (errorfds) {
        requested_error = *errorfds;
        requested_error_ptr = &requested_error;
        fdset_zero(errorfds);
    }

    for (int fd = 0; fd < nfds; fd++) {
        bool in_read = requested_read_ptr && fdset_isset(fd, requested_read_ptr);
        bool in_write = requested_write_ptr && fdset_isset(fd, requested_write_ptr);
        bool in_error = requested_error_ptr && fdset_isset(fd, requested_error_ptr);
        if (!in_read && !in_write && !in_error) {
            continue;
        }

        if (fd >= NR_OPEN_DEFAULT || !fdtable_is_used_impl(fd)) {
            errno = EBADF;
            return -1;
        }
        requested++;
    }

    timeout_ms = timeval_to_timeout_ms(timeout);
    if (timeout_ms == -2) {
        return -1;
    }

    if (requested == 0) {
        int ret = poll_impl_common(NULL, 0, timeout_ms, false);
        if (ret < 0 && errno == EINTR) {
            task_restart_record_impl(task_current(), TASK_RESTART_SELECT,
                                     (uint64_t)(int64_t)nfds,
                                     (uint64_t)(uintptr_t)readfds,
                                     (uint64_t)(uintptr_t)writefds,
                                     (uint64_t)(uintptr_t)errorfds,
                                     (uint64_t)(uintptr_t)timeout,
                                     0);
        }
        return ret;
    }

    pfds = calloc((size_t)requested, sizeof(struct pollfd));
    if (!pfds) {
        errno = ENOMEM;
        return -1;
    }

    int idx = 0;
    for (int fd = 0; fd < nfds; fd++) {
        bool in_read = requested_read_ptr && fdset_isset(fd, requested_read_ptr);
        bool in_write = requested_write_ptr && fdset_isset(fd, requested_write_ptr);
        bool in_error = requested_error_ptr && fdset_isset(fd, requested_error_ptr);
        if (!in_read && !in_write && !in_error) {
            continue;
        }
        if (in_read) {
            pfds[idx].events |= POLLIN | POLLRDNORM;
        }
        if (in_write) {
            pfds[idx].events |= POLLOUT | POLLWRNORM;
        }
        if (in_error) {
            pfds[idx].events |= POLLPRI;
        }
        pfds[idx].fd = fd;
        idx++;
    }

    int ret = poll_impl_common(pfds, (__kernel_ulong_t)requested, timeout_ms, false);
    if (ret < 0) {
        if (errno == EINTR) {
            if (readfds && requested_read_ptr) {
                *readfds = requested_read;
            }
            if (writefds && requested_write_ptr) {
                *writefds = requested_write;
            }
            if (errorfds && requested_error_ptr) {
                *errorfds = requested_error;
            }
            task_restart_record_impl(task_current(), TASK_RESTART_SELECT,
                                     (uint64_t)(int64_t)nfds,
                                     (uint64_t)(uintptr_t)readfds,
                                     (uint64_t)(uintptr_t)writefds,
                                     (uint64_t)(uintptr_t)errorfds,
                                     (uint64_t)(uintptr_t)timeout,
                                     0);
        }
        free(pfds);
        return -1;
    }

    int ready_fds = 0;
    for (int i = 0; i < requested; i++) {
        short revents = pfds[i].revents;
        int fd = pfds[i].fd;
        bool marked = false;

        if (readfds && (revents & (POLLIN | POLLRDNORM | POLLHUP))) {
            fdset_set(fd, readfds);
            marked = true;
        }
        if (writefds && (revents & (POLLOUT | POLLWRNORM))) {
            fdset_set(fd, writefds);
            marked = true;
        }
        if (errorfds && (revents & (POLLERR | POLLPRI | POLLNVAL))) {
            fdset_set(fd, errorfds);
            marked = true;
        }
        if (marked) {
            ready_fds++;
        }
    }

    free(pfds);
    return ready_fds;
}

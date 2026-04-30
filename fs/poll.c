#include <errno.h>
#include <limits.h>
#include <poll.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/select.h>
#include <sys/time.h>

#include "fdtable.h"
#include "internal/ios/fs/poll_host.h"
#include "internal/ios/fs/sync.h"
#include "pipe.h"
#include "pty.h"

static int host_poll_wait(struct pollfd *fds, nfds_t nfds, int timeout) {
    return host_poll_impl(fds, nfds, timeout);
}

static bool synthetic_fd_read_ready(int fd) {
    fd_entry_t *entry = get_fd_entry_impl(fd);
    if (!entry) {
        return false;
    }

    bool ready = get_fd_is_synthetic_proc_file_impl(entry) ||
                 get_fd_is_synthetic_dir_impl(entry) ||
                 get_fd_is_synthetic_dev_impl(entry);

    put_fd_entry_impl(entry);
    return ready;
}

static bool synthetic_fd_write_ready(int fd) {
    fd_entry_t *entry = get_fd_entry_impl(fd);
    if (!entry) {
        return false;
    }

    bool ready = false;
    if (get_fd_is_synthetic_proc_file_impl(entry)) {
        ready = true;
    } else if (get_fd_is_synthetic_dev_impl(entry)) {
        ready = true;
    }

    put_fd_entry_impl(entry);
    return ready;
}

int poll_impl(struct pollfd *fds, nfds_t nfds, int timeout) {
    if (!fds && nfds > 0) {
        errno = EFAULT;
        return -1;
    }

    if (nfds == 0) {
        if (timeout <= 0) {
            return 0;
        }
        int ret = host_poll_wait(NULL, 0, timeout);
        if (ret < 0) {
            return -1;
        }
        return 0;
    }

    int ready_count = 0;
    int host_fds_count = 0;
    int blocking_pipe_index = -1;
    struct pipe_endpoint *blocking_pipe_endpoint = NULL;
    short blocking_pipe_events = 0;
    struct pollfd *host_fds = calloc(nfds, sizeof(struct pollfd));
    int *fd_map = calloc(nfds, sizeof(int));
    if (!host_fds || !fd_map) {
        free(host_fds);
        free(fd_map);
        errno = ENOMEM;
        return -1;
    }

    for (nfds_t i = 0; i < nfds; i++) {
        int fd = fds[i].fd;
        short events = fds[i].events;
        short revents = 0;

        if (fd < 0) {
            fds[i].revents = 0;
            continue;
        }

        if (fd >= NR_OPEN_DEFAULT) {
            fds[i].revents = POLLNVAL;
            ready_count++;
            continue;
        }

        fd_entry_t *entry = get_fd_entry_impl(fd);
        if (!entry) {
            fds[i].revents = POLLNVAL;
            ready_count++;
            continue;
        }

        bool is_pty = get_fd_is_synthetic_pty_impl(entry);
        bool is_pipe = get_fd_is_pipe_impl(entry);
        bool is_synthetic = get_fd_is_synthetic_proc_file_impl(entry) ||
                            get_fd_is_synthetic_dir_impl(entry) ||
                            get_fd_is_synthetic_dev_impl(entry);
        struct pipe_endpoint *pipe_endpoint = NULL;
        unsigned int pty_index = 0;
        bool pty_is_master = false;
        if (is_pipe) {
            pipe_endpoint = get_fd_pipe_endpoint_impl(entry);
        }
        if (is_pty) {
            pty_index = get_fd_synthetic_pty_index_impl(entry);
            pty_is_master = get_fd_is_synthetic_pty_master_impl(entry);
        }
        put_fd_entry_impl(entry);

        if (is_pipe) {
            revents = pipe_poll_revents_impl(pipe_endpoint, events);
            fds[i].revents = revents;
            if (revents != 0) {
                ready_count++;
            } else if (timeout < 0 && nfds == 1) {
                blocking_pipe_index = (int)i;
                blocking_pipe_endpoint = pipe_endpoint;
                blocking_pipe_events = events;
            }
            continue;
        }

        if (is_pty) {
            revents = pty_poll_revents_impl(pty_index, pty_is_master, events);
            fds[i].revents = revents;
            if (revents != 0) {
                ready_count++;
            }
            continue;
        }

        if (is_synthetic) {
            if (events & (POLLIN | POLLRDNORM)) {
                if (synthetic_fd_read_ready(fd)) {
                    revents |= (events & (POLLIN | POLLRDNORM));
                }
            }
            if (events & (POLLOUT | POLLWRNORM)) {
                if (synthetic_fd_write_ready(fd)) {
                    revents |= (events & (POLLOUT | POLLWRNORM));
                }
            }
            fds[i].revents = revents;
            if (revents != 0) {
                ready_count++;
            }
            continue;
        }

        host_fds[host_fds_count].fd = fd;
        host_fds[host_fds_count].events = events;
        host_fds[host_fds_count].revents = 0;
        fd_map[host_fds_count] = (int)i;
        host_fds_count++;
    }

    if (host_fds_count > 0) {
        int host_timeout = (ready_count > 0) ? 0 : timeout;
        int host_ready = host_poll_wait(host_fds, (nfds_t)host_fds_count, host_timeout);
        if (host_ready < 0) {
            free(host_fds);
            free(fd_map);
            return -1;
        }

        for (int i = 0; i < host_fds_count; i++) {
            int orig_idx = fd_map[i];
            if (orig_idx >= 0 && orig_idx < (int)nfds) {
                fds[orig_idx].revents = host_fds[i].revents;
                if (host_fds[i].revents != 0) {
                    ready_count++;
                }
            }
        }
    }

    if (ready_count == 0 && host_fds_count == 0 && blocking_pipe_index >= 0) {
        short pipe_revents = pipe_poll_wait_revents_impl(blocking_pipe_endpoint, blocking_pipe_events);
        if (pipe_revents < 0) {
            free(host_fds);
            free(fd_map);
            return -1;
        }
        fds[blocking_pipe_index].revents = pipe_revents;
        if (pipe_revents != 0) {
            ready_count++;
        }
    }

    free(host_fds);
    free(fd_map);
    return ready_count;
}

static int timeval_to_timeout_ms(const struct timeval *timeout) {
    if (!timeout) {
        return -1;
    }
    if (timeout->tv_sec < 0 || timeout->tv_usec < 0) {
        errno = EINVAL;
        return -2;
    }

    uint64_t sec_ms = (uint64_t)timeout->tv_sec * 1000ULL;
    uint64_t usec_ms = (uint64_t)timeout->tv_usec / 1000ULL;
    uint64_t total = sec_ms + usec_ms;
    if (total > (uint64_t)INT32_MAX) {
        total = (uint64_t)INT32_MAX;
    }
    return (int)total;
}

int select_impl(int nfds, fd_set *readfds, fd_set *writefds, fd_set *errorfds, struct timeval *timeout) {
    if (nfds < 0) {
        errno = EINVAL;
        return -1;
    }

    fd_set requested_read;
    fd_set requested_write;
    fd_set requested_error;
    fd_set *requested_read_ptr = NULL;
    fd_set *requested_write_ptr = NULL;
    fd_set *requested_error_ptr = NULL;

    if (readfds) {
        requested_read = *readfds;
        requested_read_ptr = &requested_read;
    }
    if (writefds) {
        requested_write = *writefds;
        requested_write_ptr = &requested_write;
    }
    if (errorfds) {
        requested_error = *errorfds;
        requested_error_ptr = &requested_error;
    }

    int requested = 0;
    for (int fd = 0; fd < nfds; fd++) {
        bool in_read = requested_read_ptr && FD_ISSET(fd, requested_read_ptr);
        bool in_write = requested_write_ptr && FD_ISSET(fd, requested_write_ptr);
        bool in_error = requested_error_ptr && FD_ISSET(fd, requested_error_ptr);
        if (!in_read && !in_write && !in_error) {
            continue;
        }

        if (fd >= NR_OPEN_DEFAULT) {
            errno = EBADF;
            return -1;
        }

        fd_entry_t *entry = get_fd_entry_impl(fd);
        if (!entry) {
            errno = EBADF;
            return -1;
        }
        put_fd_entry_impl(entry);
        requested++;
    }

    if (readfds) {
        FD_ZERO(readfds);
    }
    if (writefds) {
        FD_ZERO(writefds);
    }
    if (errorfds) {
        FD_ZERO(errorfds);
    }

    if (requested == 0) {
        int timeout_ms = timeval_to_timeout_ms(timeout);
        if (timeout_ms == -2) {
            return -1;
        }
        return poll_impl(NULL, 0, timeout_ms);
    }

    struct pollfd *pfds = calloc((size_t)requested, sizeof(struct pollfd));
    if (!pfds) {
        errno = ENOMEM;
        return -1;
    }

    int idx = 0;
    for (int fd = 0; fd < nfds; fd++) {
        bool in_read = requested_read_ptr && FD_ISSET(fd, requested_read_ptr);
        bool in_write = requested_write_ptr && FD_ISSET(fd, requested_write_ptr);
        bool in_error = requested_error_ptr && FD_ISSET(fd, requested_error_ptr);
        if (!in_read && !in_write && !in_error) {
            continue;
        }

        short events = 0;
        if (in_read) {
            events |= (POLLIN | POLLRDNORM);
        }
        if (in_write) {
            events |= (POLLOUT | POLLWRNORM);
        }
        if (in_error) {
            events |= POLLPRI;
        }

        pfds[idx].fd = fd;
        pfds[idx].events = events;
        pfds[idx].revents = 0;
        idx++;
    }

    int timeout_ms = timeval_to_timeout_ms(timeout);
    if (timeout_ms == -2) {
        free(pfds);
        return -1;
    }

    int ret = poll_impl(pfds, (nfds_t)requested, timeout_ms);
    if (ret < 0) {
        free(pfds);
        return -1;
    }

    int ready_fds = 0;
    for (int i = 0; i < requested; i++) {
        short revents = pfds[i].revents;
        int fd = pfds[i].fd;
        bool marked = false;

        if (readfds && (revents & (POLLIN | POLLRDNORM))) {
            FD_SET(fd, readfds);
            marked = true;
        }
        if (writefds && (revents & (POLLOUT | POLLWRNORM))) {
            FD_SET(fd, writefds);
            marked = true;
        }
        if (errorfds && (revents & (POLLERR | POLLPRI))) {
            FD_SET(fd, errorfds);
            marked = true;
        }

        if (marked) {
            ready_fds++;
        }
    }

    free(pfds);
    return ready_fds;
}

__attribute__((visibility("default"))) int poll(struct pollfd *fds, nfds_t nfds, int timeout) {
    return poll_impl(fds, nfds, timeout);
}

__attribute__((visibility("default"))) int select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *errorfds, struct timeval *timeout) {
    return select_impl(nfds, readfds, writefds, errorfds, timeout);
}

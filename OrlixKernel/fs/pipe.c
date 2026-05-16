#include <linux/errno.h>
#include <linux/atomic.h>
#include <linux/fcntl.h>
#include <linux/poll.h>
#include <linux/signal.h>

#include "pipe.h"
#include "private/fs/fdtable_state.h"
#include "private/fs/pipe_state.h"
#include "private/fs/readiness_state.h"

#include <linux/string.h>

#include "internal/slab.h"
#include "../kernel/signal.h"
#include "../kernel/task.h"
#include "../private/kernel/task_state.h"
#include "../private/kernel/wait_queue_state.h"
#include "../kernel/wait_queue.h"

#define PIPE_BUFFER_SIZE 65536U

struct pipe_object {
    unsigned char buffer[PIPE_BUFFER_SIZE];
    size_t head;
    size_t len;
    int readers;
    int writers;
    int refs;
    unsigned long long id;
    struct wait_queue_head wait;
};

struct pipe_endpoint {
    struct pipe_object *pipe;
    bool read_end;
};

static atomic64_t next_pipe_id = ATOMIC64_INIT(1);

static size_t pipe_space_locked(const struct pipe_object *pipe) {
    return PIPE_BUFFER_SIZE - pipe->len;
}

static void pipe_free_endpoint(struct pipe_endpoint *endpoint) {
    kfree(endpoint);
}

static int pipe_create_endpoint_pair(struct pipe_endpoint **read_end,
                                     struct pipe_endpoint **write_end) {
    struct pipe_object *pipe;
    struct pipe_endpoint *reader;
    struct pipe_endpoint *writer;

    if (!read_end || !write_end) {
        return -EINVAL;
    }

    pipe = __kmalloc_noprof(sizeof(*pipe), GFP_KERNEL);
    reader = __kmalloc_noprof(sizeof(*reader), GFP_KERNEL);
    writer = __kmalloc_noprof(sizeof(*writer), GFP_KERNEL);
    if (!pipe || !reader || !writer) {
        kfree(pipe);
        kfree(reader);
        kfree(writer);
        return -ENOMEM;
    }
    memset(pipe, 0, sizeof(*pipe));
    memset(reader, 0, sizeof(*reader));
    memset(writer, 0, sizeof(*writer));

    pipe->readers = 1;
    pipe->writers = 1;
    pipe->refs = 2;
    pipe->id = (unsigned long long)atomic64_inc_return(&next_pipe_id);
    wait_queue_init(&pipe->wait);

    reader->pipe = pipe;
    reader->read_end = true;
    writer->pipe = pipe;
    writer->read_end = false;

    *read_end = reader;
    *write_end = writer;
    return 0;
}

void pipe_close_endpoint_impl(struct pipe_endpoint *endpoint) {
    struct pipe_object *pipe;
    bool should_free_pipe = false;

    if (!endpoint || !endpoint->pipe) {
        return;
    }

    pipe = endpoint->pipe;
    wait_queue_lock(&pipe->wait);
    if (endpoint->read_end) {
        if (pipe->readers > 0) {
            pipe->readers--;
        }
    } else {
        if (pipe->writers > 0) {
            pipe->writers--;
        }
    }
    pipe->refs--;
    should_free_pipe = pipe->refs == 0;
    wait_queue_wake_all_locked(&pipe->wait);
    wait_queue_unlock(&pipe->wait);

    pipe_free_endpoint(endpoint);
    if (should_free_pipe) {
        wait_queue_destroy(&pipe->wait);
        kfree(pipe);
    }
    poll_notify_readiness_impl();
}

unsigned long long pipe_endpoint_id_impl(struct pipe_endpoint *endpoint) {
    if (!endpoint || !endpoint->pipe) {
        return 0;
    }
    return endpoint->pipe->id;
}

bool pipe_endpoint_is_read_end_impl(struct pipe_endpoint *endpoint) {
    return endpoint && endpoint->read_end;
}

ssize_t pipe_read_endpoint_impl(struct pipe_endpoint *endpoint, void *buf, size_t count, bool nonblock) {
    struct pipe_object *pipe;
    size_t to_read;
    size_t first;

    if (!endpoint || !endpoint->pipe || !endpoint->read_end) {
        return -EBADF;
    }
    if (!buf && count > 0) {
        return -EFAULT;
    }
    if (count == 0) {
        return 0;
    }

    pipe = endpoint->pipe;
    wait_queue_lock(&pipe->wait);
    while (pipe->len == 0) {
        if (pipe->writers == 0) {
            wait_queue_unlock(&pipe->wait);
            return 0;
        }
        if (nonblock) {
            wait_queue_unlock(&pipe->wait);
            return -EAGAIN;
        }
        if (wait_queue_wait_locked_interruptible(&pipe->wait) != 0) {
            wait_queue_unlock(&pipe->wait);
            return -EINTR;
        }
    }

    to_read = count < pipe->len ? count : pipe->len;
    first = to_read;
    if (first > PIPE_BUFFER_SIZE - pipe->head) {
        first = PIPE_BUFFER_SIZE - pipe->head;
    }
    memcpy(buf, pipe->buffer + pipe->head, first);
    if (first < to_read) {
        memcpy((unsigned char *)buf + first, pipe->buffer, to_read - first);
    }
    pipe->head = (pipe->head + to_read) % PIPE_BUFFER_SIZE;
    pipe->len -= to_read;
    wait_queue_wake_all_locked(&pipe->wait);
    wait_queue_unlock(&pipe->wait);
    poll_notify_readiness_impl();
    return (ssize_t)to_read;
}

ssize_t pipe_write_endpoint_impl(struct pipe_endpoint *endpoint, const void *buf, size_t count, bool nonblock) {
    struct pipe_object *pipe;
    size_t space;
    size_t to_write;
    size_t tail;
    size_t first;

    if (!endpoint || !endpoint->pipe || endpoint->read_end) {
        return -EBADF;
    }
    if (!buf && count > 0) {
        return -EFAULT;
    }
    if (count == 0) {
        return 0;
    }

    pipe = endpoint->pipe;
    wait_queue_lock(&pipe->wait);
    if (pipe->readers == 0) {
        wait_queue_unlock(&pipe->wait);
        signal_generate_task(task_current(), SIGPIPE);
        return -EPIPE;
    }

    space = pipe_space_locked(pipe);
    while (space == 0) {
        if (pipe->readers == 0) {
            wait_queue_unlock(&pipe->wait);
            signal_generate_task(task_current(), SIGPIPE);
            return -EPIPE;
        }
        if (nonblock) {
            wait_queue_unlock(&pipe->wait);
            return -EAGAIN;
        }
        if (wait_queue_wait_locked_interruptible(&pipe->wait) != 0) {
            wait_queue_unlock(&pipe->wait);
            return -EINTR;
        }
        space = pipe_space_locked(pipe);
    }

    to_write = count < space ? count : space;
    tail = (pipe->head + pipe->len) % PIPE_BUFFER_SIZE;
    first = to_write;
    if (first > PIPE_BUFFER_SIZE - tail) {
        first = PIPE_BUFFER_SIZE - tail;
    }
    memcpy(pipe->buffer + tail, buf, first);
    if (first < to_write) {
        memcpy(pipe->buffer, (const unsigned char *)buf + first, to_write - first);
    }
    pipe->len += to_write;
    wait_queue_wake_all_locked(&pipe->wait);
    wait_queue_unlock(&pipe->wait);
    poll_notify_readiness_impl();
    return (ssize_t)to_write;
}

ssize_t pipe_peek_endpoint_impl(struct pipe_endpoint *endpoint, void *buf, size_t count) {
    struct pipe_object *pipe;
    size_t to_read;
    size_t first;

    if (!endpoint || !endpoint->pipe || !endpoint->read_end) {
        return -EBADF;
    }
    if (!buf && count > 0) {
        return -EFAULT;
    }
    if (count == 0) {
        return 0;
    }

    pipe = endpoint->pipe;
    wait_queue_lock(&pipe->wait);
    to_read = count < pipe->len ? count : pipe->len;
    first = to_read;
    if (first > PIPE_BUFFER_SIZE - pipe->head) {
        first = PIPE_BUFFER_SIZE - pipe->head;
    }
    if (to_read > 0) {
        memcpy(buf, pipe->buffer + pipe->head, first);
        if (first < to_read) {
            memcpy((unsigned char *)buf + first, pipe->buffer, to_read - first);
        }
    }
    wait_queue_unlock(&pipe->wait);
    return (ssize_t)to_read;
}

ssize_t pipe_tee_between_endpoints_impl(struct pipe_endpoint *src, struct pipe_endpoint *dst, size_t count, bool nonblock) {
    struct pipe_object *src_pipe;
    struct pipe_object *dst_pipe;
    unsigned char buffer[PIPE_BUFFER_SIZE];
    size_t to_copy;
    size_t src_first;
    size_t dst_tail;
    size_t dst_first;

    if (!src || !dst || !src->pipe || !dst->pipe) {
        return -EBADF;
    }
    if (count == 0) {
        return 0;
    }

    src_pipe = src->pipe;
    dst_pipe = dst->pipe;

    wait_queue_lock(&src_pipe->wait);
    to_copy = count < src_pipe->len ? count : src_pipe->len;
    src_first = to_copy;
    if (src_first > PIPE_BUFFER_SIZE - src_pipe->head) {
        src_first = PIPE_BUFFER_SIZE - src_pipe->head;
    }
    if (to_copy > 0) {
        memcpy(buffer, src_pipe->buffer + src_pipe->head, src_first);
        if (src_first < to_copy) {
            memcpy(buffer + src_first, src_pipe->buffer, to_copy - src_first);
        }
    }
    wait_queue_unlock(&src_pipe->wait);

    if (to_copy == 0) {
        return 0;
    }

    wait_queue_lock(&dst_pipe->wait);
    while (pipe_space_locked(dst_pipe) == 0) {
        if (dst_pipe->readers == 0) {
            wait_queue_unlock(&dst_pipe->wait);
            return -EPIPE;
        }
        if (nonblock) {
            wait_queue_unlock(&dst_pipe->wait);
            return -EAGAIN;
        }
        if (wait_queue_wait_locked_interruptible(&dst_pipe->wait) != 0) {
            wait_queue_unlock(&dst_pipe->wait);
            return -EINTR;
        }
    }
    if (to_copy > pipe_space_locked(dst_pipe)) {
        to_copy = pipe_space_locked(dst_pipe);
    }
    dst_tail = (dst_pipe->head + dst_pipe->len) % PIPE_BUFFER_SIZE;
    dst_first = to_copy;
    if (dst_first > PIPE_BUFFER_SIZE - dst_tail) {
        dst_first = PIPE_BUFFER_SIZE - dst_tail;
    }
    memcpy(dst_pipe->buffer + dst_tail, buffer, dst_first);
    if (dst_first < to_copy) {
        memcpy(dst_pipe->buffer, buffer + dst_first, to_copy - dst_first);
    }
    dst_pipe->len += to_copy;
    wait_queue_wake_all_locked(&dst_pipe->wait);
    wait_queue_unlock(&dst_pipe->wait);
    poll_notify_readiness_impl();
    return (ssize_t)to_copy;
}

short pipe_poll_revents_impl(struct pipe_endpoint *endpoint, short events) {
    struct pipe_object *pipe;
    short revents = 0;

    if (!endpoint || !endpoint->pipe) {
        return POLLNVAL;
    }

    pipe = endpoint->pipe;
    wait_queue_lock(&pipe->wait);
    if (endpoint->read_end) {
        if ((events & (POLLIN | POLLRDNORM)) && pipe->len > 0) {
            revents |= events & (POLLIN | POLLRDNORM);
        }
        if (pipe->writers == 0) {
            revents |= POLLHUP;
        }
    } else {
        if (pipe->readers == 0) {
            revents |= POLLERR;
        } else if ((events & (POLLOUT | POLLWRNORM)) && pipe_space_locked(pipe) > 0) {
            revents |= events & (POLLOUT | POLLWRNORM);
        }
    }
    wait_queue_unlock(&pipe->wait);
    return revents;
}

short pipe_poll_wait_revents_impl(struct pipe_endpoint *endpoint, short events) {
    struct pipe_object *pipe;
    short revents;

    if (!endpoint || !endpoint->pipe) {
        return POLLNVAL;
    }

    pipe = endpoint->pipe;
    wait_queue_lock(&pipe->wait);
    for (;;) {
        revents = 0;
        if (endpoint->read_end) {
            if ((events & (POLLIN | POLLRDNORM)) && pipe->len > 0) {
                revents |= events & (POLLIN | POLLRDNORM);
            }
            if (pipe->writers == 0) {
                revents |= POLLHUP;
            }
        } else {
            if (pipe->readers == 0) {
                revents |= POLLERR;
            } else if ((events & (POLLOUT | POLLWRNORM)) && pipe_space_locked(pipe) > 0) {
                revents |= events & (POLLOUT | POLLWRNORM);
            }
        }

        if (revents != 0) {
            wait_queue_unlock(&pipe->wait);
            return revents;
        }

        if (wait_queue_wait_locked_interruptible(&pipe->wait) != 0) {
            wait_queue_unlock(&pipe->wait);
            return -EINTR;
        }
    }
}

void pipe_poll_wait_queue_impl(struct pipe_endpoint *endpoint, struct wait_queue_head **queue_out) {
    if (!queue_out) {
        return;
    }
    *queue_out = NULL;
    if (endpoint && endpoint->pipe) {
        *queue_out = &endpoint->pipe->wait;
    }
}

int pipe2_impl(int pipefd[2], int flags) {
    struct pipe_endpoint *read_end = NULL;
    struct pipe_endpoint *write_end = NULL;
    int read_fd;
    int write_fd;
    int supported_flags = O_CLOEXEC | O_NONBLOCK;

    if (!pipefd) {
        return -EFAULT;
    }
    if (flags & ~supported_flags) {
        return -EINVAL;
    }
    if (pipe_create_endpoint_pair(&read_end, &write_end) != 0) {
        return -ENOMEM;
    }

    read_fd = alloc_fd_impl();
    if (read_fd < 0) {
        pipe_close_endpoint_impl(read_end);
        pipe_close_endpoint_impl(write_end);
        return read_fd;
    }
    if (init_pipe_fd_entry_impl(read_fd, O_RDONLY | (flags & supported_flags), read_end) != 0) {
        free_fd_impl(read_fd);
        pipe_close_endpoint_impl(read_end);
        pipe_close_endpoint_impl(write_end);
        return -ENOMEM;
    }

    write_fd = alloc_fd_impl();
    if (write_fd < 0) {
        free_fd_impl(read_fd);
        pipe_close_endpoint_impl(write_end);
        return write_fd;
    }
    if (init_pipe_fd_entry_impl(write_fd, O_WRONLY | (flags & supported_flags), write_end) != 0) {
        free_fd_impl(read_fd);
        free_fd_impl(write_fd);
        pipe_close_endpoint_impl(write_end);
        return -ENOMEM;
    }

    pipefd[0] = read_fd;
    pipefd[1] = write_fd;
    return 0;
}

int pipe_impl(int pipefd[2]) {
    return pipe2_impl(pipefd, 0);
}

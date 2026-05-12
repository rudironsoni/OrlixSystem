#include <uapi/asm/ioctls.h>
#include <uapi/asm/unistd.h>
#include <uapi/linux/errno.h>
#include <uapi/linux/fcntl.h>
#include <uapi/linux/pidfd.h>
#include <uapi/linux/poll.h>
#include <uapi/linux/time.h>
#include <uapi/linux/eventfd.h>
#include <uapi/linux/signal.h>
#include <uapi/linux/timerfd.h>
#include <linux/string.h>

#include "fs/fdtable.h"
#include "kernel/signal.h"
#include "private/kernel/signal_state.h"
#include "kernel/task.h"
#include "private/kernel/kthread_state.h"
#include "private/kernel/task_state.h"
#include "runtime/syscall.h"

extern int pty_contract_ioctl(int fd, unsigned long request, ...);
extern int open_impl(const char *pathname, int flags, uint32_t mode);
extern int pipe_impl(int pipefd[2]);
extern int poll_impl(struct pollfd *fds, __kernel_ulong_t nfds, int timeout);
extern int select_impl(int nfds,
                       __kernel_fd_set *readfds,
                       __kernel_fd_set *writefds,
                       __kernel_fd_set *errorfds,
                       struct __kernel_old_timeval *timeout);
extern long read_impl(int fd, void *buf, size_t count);
extern long write_impl(int fd, const void *buf, size_t count);
extern int signal_generate_task(struct task *target, int32_t sig);
extern void exit_impl(int status);
extern int errno;
extern long socketpair_stream_syscall(int fds[2]);

static int close_if_open(int fd) {
    return fd >= 0 ? close_impl(fd) : 0;
}

static void fdset_zero(__kernel_fd_set *set) {
    if (!set) {
        return;
    }
    memset(set->fds_bits, 0, sizeof(set->fds_bits));
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

static int fdset_isset(int fd, const __kernel_fd_set *set) {
    unsigned int bits_per_word = (unsigned int)(8U * sizeof(set->fds_bits[0]));
    unsigned int word;
    unsigned int bit;

    if (!set || fd < 0 || fd >= __FD_SETSIZE) {
        return 0;
    }
    word = (unsigned int)fd / bits_per_word;
    bit = (unsigned int)fd % bits_per_word;
    return (set->fds_bits[word] & (1UL << bit)) != 0;
}

int readiness_contract_eventfd2_counter_read_write_and_poll(void) {
    struct pollfd pfd;
    uint64_t value;
    int efd = -1;
    int semfd = -1;
    long ret;

    efd = (int)syscall_dispatch_impl(__NR_eventfd2, 3, EFD_NONBLOCK | EFD_CLOEXEC, 0, 0, 0, 0);
    if (efd < 0) {
        errno = (int)-efd;
        return -1;
    }

    ret = syscall_dispatch_impl(__NR_fcntl, efd, F_GETFD, 0, 0, 0, 0);
    if (ret != FD_CLOEXEC) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }

    memset(&pfd, 0, sizeof(pfd));
    pfd.fd = efd;
    pfd.events = POLLIN | POLLOUT;
    ret = syscall_dispatch_impl(__NR_ppoll, (long)(uintptr_t)&pfd, 1, 0, 0, 0, 0);
    if (ret != 1 || (pfd.revents & POLLIN) == 0 || (pfd.revents & POLLOUT) == 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }

    value = 0;
    ret = syscall_dispatch_impl(__NR_read, efd, (long)(uintptr_t)&value, sizeof(value), 0, 0, 0);
    if (ret != (long)sizeof(value) || value != 3) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }
    ret = syscall_dispatch_impl(__NR_read, efd, (long)(uintptr_t)&value, sizeof(value), 0, 0, 0);
    if (ret != -EAGAIN) {
        errno = ret < 0 ? (int)-ret : EAGAIN;
        goto out;
    }

    value = 5;
    ret = syscall_dispatch_impl(__NR_write, efd, (long)(uintptr_t)&value, sizeof(value), 0, 0, 0);
    if (ret != (long)sizeof(value)) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }
    value = 0;
    ret = syscall_dispatch_impl(__NR_read, efd, (long)(uintptr_t)&value, sizeof(value), 0, 0, 0);
    if (ret != (long)sizeof(value) || value != 5) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }

    semfd = (int)syscall_dispatch_impl(__NR_eventfd2, 2, EFD_NONBLOCK | EFD_SEMAPHORE, 0, 0, 0, 0);
    if (semfd < 0) {
        errno = (int)-semfd;
        goto out;
    }
    value = 0;
    ret = syscall_dispatch_impl(__NR_read, semfd, (long)(uintptr_t)&value, sizeof(value), 0, 0, 0);
    if (ret != (long)sizeof(value) || value != 1) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }
    value = 0;
    ret = syscall_dispatch_impl(__NR_read, semfd, (long)(uintptr_t)&value, sizeof(value), 0, 0, 0);
    if (ret != (long)sizeof(value) || value != 1) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }

    close_if_open(efd);
    close_if_open(semfd);
    return 0;

out:
    close_if_open(efd);
    close_if_open(semfd);
    return -1;
}

int readiness_contract_timerfd_relative_expiration_read_and_poll(void) {
    struct __kernel_itimerspec spec;
    struct __kernel_itimerspec current_spec;
    struct pollfd pfd;
    uint64_t expirations = 0;
    int fd = -1;
    long ret;

    fd = (int)syscall_dispatch_impl(__NR_timerfd_create, 1, TFD_NONBLOCK | TFD_CLOEXEC, 0, 0, 0, 0);
    if (fd < 0) {
        errno = (int)-fd;
        return -101;
    }

    ret = syscall_dispatch_impl(__NR_fcntl, fd, F_GETFD, 0, 0, 0, 0);
    if (ret != FD_CLOEXEC) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }

    memset(&current_spec, 0, sizeof(current_spec));
    ret = syscall_dispatch_impl(__NR_timerfd_gettime, fd, (long)(uintptr_t)&current_spec, 0, 0, 0, 0);
    if (ret != 0 || current_spec.it_value.tv_sec != 0 || current_spec.it_value.tv_nsec != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }

    memset(&spec, 0, sizeof(spec));
    spec.it_value.tv_nsec = 1;
    ret = syscall_dispatch_impl(__NR_timerfd_settime, fd, 0, (long)(uintptr_t)&spec,
                                (long)(uintptr_t)&current_spec, 0, 0);
    if (ret != 0 || current_spec.it_value.tv_sec != 0 || current_spec.it_value.tv_nsec != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }

    memset(&pfd, 0, sizeof(pfd));
    pfd.fd = fd;
    pfd.events = POLLIN;
    ret = syscall_dispatch_impl(__NR_ppoll, (long)(uintptr_t)&pfd, 1, 0, 0, 0, 0);
    if (ret != 1 || (pfd.revents & POLLIN) == 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }

    ret = syscall_dispatch_impl(__NR_read, fd, (long)(uintptr_t)&expirations,
                                sizeof(expirations), 0, 0, 0);
    if (ret != (long)sizeof(expirations) || expirations < 1) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }

    memset(&current_spec, 0, sizeof(current_spec));
    ret = syscall_dispatch_impl(__NR_timerfd_gettime, fd, (long)(uintptr_t)&current_spec, 0, 0, 0, 0);
    if (ret != 0 || current_spec.it_value.tv_sec != 0 || current_spec.it_value.tv_nsec != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }

    close_if_open(fd);
    return 0;

out:
    close_if_open(fd);
    return -1;
}

static int append_decimal(char *buf, size_t buf_size, int value) {
    char digits[16];
    size_t count = 0;
    if (value < 0) {
        errno = EINVAL;
        return -1;
    }
    do {
        digits[count++] = (char)('0' + (value % 10));
        value /= 10;
    } while (value > 0 && count < sizeof(digits));
    if (value > 0 || count + 1 > buf_size) {
        errno = ENAMETOOLONG;
        return -1;
    }
    for (size_t i = 0; i < count; i++) {
        buf[i] = digits[count - 1 - i];
    }
    buf[count] = '\0';
    return 0;
}

static int alloc_pty_pair(int *master_fd_out, int *slave_fd_out) {
    int master_fd = -1;
    int slave_fd = -1;
    unsigned int pty_index = 0;
    int unlock = 0;
    char slave_path[64];

    master_fd = open_impl("/dev/ptmx", O_RDWR, 0);
    if (master_fd < 0) {
        return -1;
    }
    if (pty_contract_ioctl(master_fd, TIOCGPTN, &pty_index) != 0) {
        close_impl(master_fd);
        return -1;
    }
    if (pty_contract_ioctl(master_fd, TIOCSPTLCK, &unlock) != 0) {
        close_impl(master_fd);
        return -1;
    }
    memcpy(slave_path, "/dev/pts/", 9);
    if (append_decimal(slave_path + 9, sizeof(slave_path) - 9, (int)pty_index) != 0) {
        close_impl(master_fd);
        return -1;
    }
    slave_fd = open_impl(slave_path, O_RDWR, 0);
    if (slave_fd < 0) {
        close_impl(master_fd);
        return -1;
    }
    *master_fd_out = master_fd;
    *slave_fd_out = slave_fd;
    return 0;
}

struct readiness_thread_case {
    int fd;
    int write_fd;
    int mode;
    kernel_mutex_t lock;
    kernel_cond_t cond;
    int started;
    int restart_ready;
    int proceed;
    int done;
    int result;
    struct task *task;
};

struct pselect_mask_case {
    int fd;
    kernel_mutex_t lock;
    kernel_cond_t cond;
    int started;
    int done;
    int result;
    struct task *task;
};

static void case_init(struct readiness_thread_case *ctx, int fd, int mode) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->fd = fd;
    ctx->mode = mode;
    kernel_mutex_init(&ctx->lock);
    kernel_cond_init(&ctx->cond);
}

static void case_destroy(struct readiness_thread_case *ctx) {
    kernel_cond_destroy(&ctx->cond);
    kernel_mutex_destroy(&ctx->lock);
}

static void case_mark_started(struct readiness_thread_case *ctx) {
    kernel_mutex_lock(&ctx->lock);
    ctx->started = 1;
    kernel_cond_broadcast(&ctx->cond);
    kernel_mutex_unlock(&ctx->lock);
}

static void case_mark_done(struct readiness_thread_case *ctx, int result) {
    kernel_mutex_lock(&ctx->lock);
    ctx->result = result;
    ctx->done = 1;
    kernel_cond_broadcast(&ctx->cond);
    kernel_mutex_unlock(&ctx->lock);
}

static void case_mark_restart_ready(struct readiness_thread_case *ctx) {
    kernel_mutex_lock(&ctx->lock);
    ctx->restart_ready = 1;
    kernel_cond_broadcast(&ctx->cond);
    while (!ctx->proceed) {
        kernel_cond_wait(&ctx->cond, &ctx->lock);
    }
    kernel_mutex_unlock(&ctx->lock);
}

static void case_wait_restart_ready(struct readiness_thread_case *ctx) {
    kernel_mutex_lock(&ctx->lock);
    while (!ctx->restart_ready) {
        kernel_cond_wait(&ctx->cond, &ctx->lock);
    }
    kernel_mutex_unlock(&ctx->lock);
}

static void case_allow_restart(struct readiness_thread_case *ctx) {
    kernel_mutex_lock(&ctx->lock);
    ctx->proceed = 1;
    kernel_cond_broadcast(&ctx->cond);
    kernel_mutex_unlock(&ctx->lock);
}

static void case_wait_started(struct readiness_thread_case *ctx) {
    kernel_mutex_lock(&ctx->lock);
    while (!ctx->started) {
        kernel_cond_wait(&ctx->cond, &ctx->lock);
    }
    kernel_mutex_unlock(&ctx->lock);
}

static int case_wait_done(struct readiness_thread_case *ctx) {
    int result;
    kernel_mutex_lock(&ctx->lock);
    while (!ctx->done) {
        kernel_cond_wait(&ctx->cond, &ctx->lock);
    }
    result = ctx->result;
    kernel_mutex_unlock(&ctx->lock);
    return result;
}

static void pselect_case_init(struct pselect_mask_case *ctx, int fd, struct task *task) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->fd = fd;
    ctx->task = task;
    kernel_mutex_init(&ctx->lock);
    kernel_cond_init(&ctx->cond);
}

static void pselect_case_destroy(struct pselect_mask_case *ctx) {
    kernel_cond_destroy(&ctx->cond);
    kernel_mutex_destroy(&ctx->lock);
}

static void pselect_case_mark_started(struct pselect_mask_case *ctx) {
    kernel_mutex_lock(&ctx->lock);
    ctx->started = 1;
    kernel_cond_broadcast(&ctx->cond);
    kernel_mutex_unlock(&ctx->lock);
}

static void pselect_case_mark_done(struct pselect_mask_case *ctx, int result) {
    kernel_mutex_lock(&ctx->lock);
    ctx->result = result;
    ctx->done = 1;
    kernel_cond_broadcast(&ctx->cond);
    kernel_mutex_unlock(&ctx->lock);
}

static void pselect_case_wait_started(struct pselect_mask_case *ctx) {
    kernel_mutex_lock(&ctx->lock);
    while (!ctx->started) {
        kernel_cond_wait(&ctx->cond, &ctx->lock);
    }
    kernel_mutex_unlock(&ctx->lock);
}

static int pselect_case_wait_done(struct pselect_mask_case *ctx) {
    int result;
    kernel_mutex_lock(&ctx->lock);
    while (!ctx->done) {
        kernel_cond_wait(&ctx->cond, &ctx->lock);
    }
    result = ctx->result;
    kernel_mutex_unlock(&ctx->lock);
    return result;
}

static void *pselect_mask_thread(void *arg) {
    struct pselect_mask_case *ctx = arg;
    __kernel_fd_set readfds;
    sigset_t sigmask = {0};
    struct {
        const uint64_t *ss;
        size_t ss_len;
    } mask_arg = {(const uint64_t *)&sigmask, sizeof(sigmask)};
    sigset_t queried = {0};
    long ret;

    task_set_current(ctx->task);
    sigaddset(&sigmask, SIGUSR1);
    fdset_zero(&readfds);
    fdset_set(ctx->fd, &readfds);
    pselect_case_mark_started(ctx);
    ret = syscall_dispatch_impl(__NR_pselect6, ctx->fd + 1, (long)(uintptr_t)&readfds,
                                0, 0, 0, (long)(uintptr_t)&mask_arg);
    if (ret != 1 || !fdset_isset(ctx->fd, &readfds)) {
        pselect_case_mark_done(ctx, ret < 0 ? (int)-ret : EIO);
        return NULL;
    }
    ret = syscall_dispatch_impl(__NR_rt_sigprocmask, SIG_BLOCK, 0,
                                (long)(uintptr_t)&queried, sizeof(queried), 0, 0);
    if (ret != 0 || sigismember(&queried, SIGUSR1) != 0) {
        pselect_case_mark_done(ctx, ret < 0 ? (int)-ret : EBUSY);
        return NULL;
    }
    pselect_case_mark_done(ctx, 0);
    return NULL;
}

static void *poll_thread(void *arg) {
    struct readiness_thread_case *ctx = arg;
    struct pollfd pfd = {.fd = ctx->fd, .events = POLLIN, .revents = 0};
    int ret;
    if (ctx->task) {
        task_set_current(ctx->task);
    }
    case_mark_started(ctx);
    ret = poll_impl(&pfd, 1, -1);
    if (ret == 1 && (pfd.revents & POLLIN)) {
        case_mark_done(ctx, 0);
    } else if (ret == -1 && errno == EINTR) {
        case_mark_done(ctx, EINTR);
    } else {
        case_mark_done(ctx, errno ? errno : EIO);
    }
    return NULL;
}

static void *select_thread(void *arg) {
    struct readiness_thread_case *ctx = arg;
    __kernel_fd_set readfds;
    int ret;
    if (ctx->task) {
        task_set_current(ctx->task);
    }
    fdset_zero(&readfds);
    fdset_set(ctx->fd, &readfds);
    case_mark_started(ctx);
    ret = select_impl(ctx->fd + 1, &readfds, NULL, NULL, NULL);
    if (ret == 1 && fdset_isset(ctx->fd, &readfds)) {
        case_mark_done(ctx, 0);
    } else if (ret == -1 && errno == EINTR) {
        case_mark_done(ctx, EINTR);
    } else {
        case_mark_done(ctx, errno ? errno : EIO);
    }
    return NULL;
}

static void *select_restart_thread(void *arg) {
    struct readiness_thread_case *ctx = arg;
    __kernel_fd_set readfds;
    long ret;

    task_set_current(ctx->task);
    fdset_zero(&readfds);
    fdset_set(ctx->fd, &readfds);
    case_mark_started(ctx);
    ret = select_impl(ctx->fd + 1, &readfds, NULL, NULL, NULL);
    if (ret != -1 || errno != EINTR ||
        !signal_frame_restart_matches_task(ctx->task, TASK_RESTART_SELECT,
                                           (uint64_t)(int64_t)(ctx->fd + 1),
                                           (uint64_t)(uintptr_t)&readfds,
                                           0, 0, 0, 0)) {
        case_mark_done(ctx, ENODATA);
        return NULL;
    }
    signal_clear_pending_task(ctx->task, SIGUSR1);
    case_mark_restart_ready(ctx);
    ret = syscall_dispatch_impl(__NR_restart_syscall, 0, 0, 0, 0, 0, 0);
    if (ret == 1 && fdset_isset(ctx->fd, &readfds) &&
        signal_frame_restart_is_task(ctx->task, TASK_RESTART_NONE)) {
        case_mark_done(ctx, 0);
    } else {
        case_mark_done(ctx, ret < 0 ? (int)-ret : EIO);
    }
    return NULL;
}

static int run_pipe_wake_case(void *(*thread_main)(void *), int write_second_pipe) {
    int first[2] = {-1, -1};
    int second[2] = {-1, -1};
    struct readiness_thread_case ctx;
    kernel_thread_t thread;
    int ret = 0;

    if (pipe_impl(first) != 0 || pipe_impl(second) != 0) {
        ret = errno;
        goto out;
    }
    case_init(&ctx, write_second_pipe ? second[0] : first[0], 0);
    if (kernel_thread_create(&thread, NULL, thread_main, &ctx) != 0) {
        ret = errno ? errno : EIO;
        goto out_destroy;
    }
    kernel_thread_detach(thread);
    case_wait_started(&ctx);
    if (write_impl(write_second_pipe ? second[1] : first[1], "x", 1) != 1) {
        ret = errno ? errno : EIO;
        goto out_destroy;
    }
    ret = case_wait_done(&ctx);
out_destroy:
    case_destroy(&ctx);
out:
    close_if_open(first[0]);
    close_if_open(first[1]);
    close_if_open(second[0]);
    close_if_open(second[1]);
    return ret;
}

int readiness_contract_poll_pipe_blocks_until_writer_writes(void) {
    return run_pipe_wake_case(poll_thread, 0);
}

static int run_socketpair_wake_case(void *(*thread_main)(void *)) {
    int fds[2] = {-1, -1};
    struct readiness_thread_case ctx;
    kernel_thread_t thread;
    struct task *parent = task_current();
    struct task *child = NULL;
    long ret = 0;

    ret = socketpair_stream_syscall(fds);
    if (ret != 0) {
        return ret < 0 ? (int)-ret : EIO;
    }

    child = task_create_child_impl(parent);
    if (!child) {
        ret = errno ? errno : ENOMEM;
        goto out;
    }

    case_init(&ctx, fds[0], 0);
    ctx.task = child;
    if (kernel_thread_create(&thread, NULL, thread_main, &ctx) != 0) {
        ret = errno ? errno : EIO;
        goto out_destroy;
    }
    kernel_thread_detach(thread);
    case_wait_started(&ctx);
    if (write_impl(fds[1], "x", 1) != 1) {
        ret = errno ? errno : EIO;
        goto out_destroy;
    }
    ret = case_wait_done(&ctx);
out_destroy:
    case_destroy(&ctx);
    task_unlink_child_impl(parent, child);
    task_put(child);
out:
    close_if_open(fds[0]);
    close_if_open(fds[1]);
    return (int)ret;
}

int readiness_contract_poll_socketpair_blocks_until_peer_writes(void) {
    return run_socketpair_wake_case(poll_thread);
}

int readiness_contract_poll_socketpair_hup_after_peer_close(void) {
    int fds[2] = {-1, -1};
    struct pollfd pfd;
    long ret;
    int out = 0;

    ret = socketpair_stream_syscall(fds);
    if (ret != 0) {
        return ret < 0 ? (int)-ret : EIO;
    }

    if (close_impl(fds[1]) != 0) {
        out = errno ? errno : EIO;
        goto out_close;
    }
    fds[1] = -1;

    memset(&pfd, 0, sizeof(pfd));
    pfd.fd = fds[0];
    pfd.events = POLLIN;
    if (poll_impl(&pfd, 1, 0) != 1 || (pfd.revents & POLLHUP) == 0) {
        out = errno ? errno : EIO;
    }

out_close:
    close_if_open(fds[0]);
    close_if_open(fds[1]);
    return out;
}

int readiness_contract_poll_pipe_timeout_returns_zero(void) {
    int fds[2] = {-1, -1};
    struct pollfd pfd;
    int ret = 0;
    if (pipe_impl(fds) != 0) return errno;
    pfd.fd = fds[0];
    pfd.events = POLLIN;
    pfd.revents = 0;
    if (poll_impl(&pfd, 1, 5) != 0 || pfd.revents != 0) ret = errno ? errno : EIO;
    close_if_open(fds[0]);
    close_if_open(fds[1]);
    return ret;
}

int readiness_contract_poll_pipe_signal_interrupt_returns_intr(void) {
    int fds[2] = {-1, -1};
    struct readiness_thread_case ctx;
    kernel_thread_t thread;
    struct task *parent = task_current();
    struct task *child = NULL;
    int ret = 0;
    if (pipe_impl(fds) != 0) return errno;
    child = task_create_child_impl(parent);
    if (!child) {
        ret = errno ? errno : ENOMEM;
        goto out;
    }
    case_init(&ctx, fds[0], 0);
    ctx.task = child;
    if (kernel_thread_create(&thread, NULL, poll_thread, &ctx) != 0) {
        ret = errno ? errno : EIO;
        goto out_destroy;
    }
    kernel_thread_detach(thread);
    case_wait_started(&ctx);
    if (signal_generate_task(child, SIGUSR1) != 0) {
        ret = errno ? errno : EIO;
        goto out_destroy;
    }
    ret = case_wait_done(&ctx);
    if (ret == EINTR) {
        uint64_t restart_arg1 = 0;
        uint64_t restart_arg2 = 0;
        if (!signal_frame_restart_is_task(child, TASK_RESTART_POLL) ||
            signal_frame_restart_arg_get_task(child, 1, &restart_arg1) != 0 ||
            signal_frame_restart_arg_get_task(child, 2, &restart_arg2) != 0 ||
            restart_arg1 != 1 ||
            (int)restart_arg2 != -1) {
            ret = ENODATA;
        } else {
            task_restart_clear_impl(child);
            ret = 0;
        }
    }
out_destroy:
    case_destroy(&ctx);
    task_unlink_child_impl(parent, child);
    task_put(child);
out:
    close_if_open(fds[0]);
    close_if_open(fds[1]);
    return ret;
}

int readiness_contract_poll_multiple_fds_returns_first_ready_pipe(void) {
    int a[2] = {-1, -1};
    int b[2] = {-1, -1};
    struct pollfd pfds[2];
    int ret = 0;
    if (pipe_impl(a) != 0 || pipe_impl(b) != 0) return errno;
    if (write_impl(a[1], "x", 1) != 1) {
        ret = errno ? errno : EIO;
        goto out;
    }
    pfds[0].fd = a[0]; pfds[0].events = POLLIN; pfds[0].revents = 0;
    pfds[1].fd = b[0]; pfds[1].events = POLLIN; pfds[1].revents = 0;
    if (poll_impl(pfds, 2, -1) != 1 || (pfds[0].revents & POLLIN) == 0 || pfds[1].revents != 0) {
        ret = errno ? errno : EIO;
    }
out:
    close_if_open(a[0]); close_if_open(a[1]); close_if_open(b[0]); close_if_open(b[1]);
    return ret;
}

int readiness_contract_poll_multiple_fds_wakes_when_second_pipe_becomes_ready(void) {
    return run_pipe_wake_case(poll_thread, 1);
}

int readiness_contract_poll_pipe_hup_after_writer_close(void) {
    int fds[2] = {-1, -1};
    struct pollfd pfd;
    int ret = 0;
    if (pipe_impl(fds) != 0) return errno;
    close_if_open(fds[1]); fds[1] = -1;
    pfd.fd = fds[0]; pfd.events = POLLIN; pfd.revents = 0;
    if (poll_impl(&pfd, 1, 0) != 1 || (pfd.revents & POLLHUP) == 0) ret = errno ? errno : EIO;
    close_if_open(fds[0]);
    return ret;
}

int readiness_contract_poll_pipe_write_end_err_after_reader_close(void) {
    int fds[2] = {-1, -1};
    struct pollfd pfd;
    int ret = 0;
    if (pipe_impl(fds) != 0) return errno;
    close_if_open(fds[0]); fds[0] = -1;
    pfd.fd = fds[1]; pfd.events = POLLOUT; pfd.revents = 0;
    if (poll_impl(&pfd, 1, 0) != 1 || (pfd.revents & POLLERR) == 0) ret = errno ? errno : EIO;
    close_if_open(fds[1]);
    return ret;
}

static int run_pty_wake_case(int wait_fd_is_master, int select_mode) {
    int master = -1;
    int slave = -1;
    struct readiness_thread_case ctx;
    kernel_thread_t thread;
    int ret = 0;
    if (alloc_pty_pair(&master, &slave) != 0) return errno;
    case_init(&ctx, wait_fd_is_master ? master : slave, 0);
    if (kernel_thread_create(&thread, NULL, select_mode ? select_thread : poll_thread, &ctx) != 0) {
        ret = errno ? errno : EIO;
        goto out_destroy;
    }
    kernel_thread_detach(thread);
    case_wait_started(&ctx);
    if (write_impl(wait_fd_is_master ? slave : master, "x\n", 2) < 1) {
        ret = errno ? errno : EIO;
        goto out_destroy;
    }
    ret = case_wait_done(&ctx);
out_destroy:
    case_destroy(&ctx);
    close_if_open(master);
    close_if_open(slave);
    return ret;
}

int readiness_contract_poll_pty_master_blocks_until_slave_writes(void) {
    return run_pty_wake_case(1, 0);
}

int readiness_contract_poll_pty_slave_blocks_until_master_writes(void) {
    return run_pty_wake_case(0, 0);
}

int readiness_contract_poll_pty_hup_after_peer_close(void) {
    int master = -1;
    int slave = -1;
    struct pollfd pfd;
    int ret = 0;
    if (alloc_pty_pair(&master, &slave) != 0) return errno;
    close_if_open(slave); slave = -1;
    pfd.fd = master; pfd.events = POLLIN; pfd.revents = 0;
    if (poll_impl(&pfd, 1, 0) != 1 || (pfd.revents & POLLHUP) == 0) ret = errno ? errno : EIO;
    close_if_open(master);
    return ret;
}

int readiness_contract_select_pipe_read_blocks_until_writer_writes(void) {
    return run_pipe_wake_case(select_thread, 0);
}

int readiness_contract_select_pipe_write_reports_writable(void) {
    int fds[2] = {-1, -1};
    __kernel_fd_set writefds;
    int ret = 0;
    if (pipe_impl(fds) != 0) return errno;
    fdset_zero(&writefds);
    fdset_set(fds[1], &writefds);
    if (select_impl(fds[1] + 1, NULL, &writefds, NULL, NULL) != 1 || !fdset_isset(fds[1], &writefds)) ret = errno ? errno : EIO;
    close_if_open(fds[0]);
    close_if_open(fds[1]);
    return ret;
}

int readiness_contract_select_timeout_returns_zero(void) {
    int fds[2] = {-1, -1};
    __kernel_fd_set readfds;
    struct __kernel_old_timeval tv = {.tv_sec = 0, .tv_usec = 5000};
    int ret = 0;
    if (pipe_impl(fds) != 0) return errno;
    fdset_zero(&readfds);
    fdset_set(fds[0], &readfds);
    if (select_impl(fds[0] + 1, &readfds, NULL, NULL, &tv) != 0 || fdset_isset(fds[0], &readfds)) ret = errno ? errno : EIO;
    close_if_open(fds[0]);
    close_if_open(fds[1]);
    return ret;
}

int readiness_contract_select_signal_interrupt_returns_intr(void) {
    int fds[2] = {-1, -1};
    struct readiness_thread_case ctx;
    kernel_thread_t thread;
    struct task *parent = task_current();
    struct task *child = NULL;
    int ret = 0;
    if (pipe_impl(fds) != 0) return errno;
    child = task_create_child_impl(parent);
    if (!child) {
        ret = errno ? errno : ENOMEM;
        goto out;
    }
    case_init(&ctx, fds[0], 0);
    ctx.task = child;
    if (kernel_thread_create(&thread, NULL, select_thread, &ctx) != 0) {
        ret = errno ? errno : EIO;
        goto out_destroy;
    }
    kernel_thread_detach(thread);
    case_wait_started(&ctx);
    if (signal_generate_task(child, SIGUSR1) != 0) {
        ret = errno ? errno : EIO;
        goto out_destroy;
    }
    ret = case_wait_done(&ctx);
    if (ret == EINTR) {
        uint64_t restart_arg0 = 0;
        if (!signal_frame_restart_is_task(child, TASK_RESTART_SELECT) ||
            signal_frame_restart_arg_get_task(child, 0, &restart_arg0) != 0 ||
            restart_arg0 != (uint64_t)(int64_t)(fds[0] + 1)) {
            ret = ENODATA;
        } else {
            task_restart_clear_impl(child);
            ret = 0;
        }
    }
out_destroy:
    case_destroy(&ctx);
    task_unlink_child_impl(parent, child);
    task_put(child);
out:
    close_if_open(fds[0]);
    close_if_open(fds[1]);
    return ret;
}

int readiness_contract_select_restart_syscall_reenters_readiness_wait(void) {
    int fds[2] = {-1, -1};
    struct readiness_thread_case ctx;
    kernel_thread_t thread;
    struct task *parent = task_current();
    struct task *child = NULL;
    int ret = 0;

    if (pipe_impl(fds) != 0) return errno;
    child = task_create_child_impl(parent);
    if (!child) {
        ret = errno ? errno : ENOMEM;
        goto out;
    }
    case_init(&ctx, fds[0], 0);
    ctx.write_fd = fds[1];
    ctx.task = child;
    if (kernel_thread_create(&thread, NULL, select_restart_thread, &ctx) != 0) {
        ret = errno ? errno : EIO;
        goto out_destroy;
    }
    kernel_thread_detach(thread);
    case_wait_started(&ctx);
    if (signal_generate_task(child, SIGUSR1) != 0) {
        ret = errno ? errno : EIO;
        goto out_destroy;
    }
    case_wait_restart_ready(&ctx);
    if (write_impl(ctx.write_fd, "r", 1) != 1) {
        ret = errno ? errno : EIO;
        goto out_destroy;
    }
    case_allow_restart(&ctx);
    ret = case_wait_done(&ctx);
out_destroy:
    case_destroy(&ctx);
    task_unlink_child_impl(parent, child);
    task_put(child);
out:
    close_if_open(fds[0]);
    close_if_open(fds[1]);
    return ret;
}

int readiness_contract_select_pty_read_wakes_on_peer_write(void) {
    return run_pty_wake_case(1, 1);
}

int readiness_contract_pselect6_pipe_uses_shared_readiness_engine(void) {
    int fds[2] = {-1, -1};
    __kernel_fd_set readfds;
    struct __kernel_timespec timeout;
    long ret;
    int result = -1;

    if (pipe_impl(fds) != 0) {
        return errno;
    }
    if (write_impl(fds[1], "p", 1) != 1) {
        result = errno ? errno : EIO;
        goto out;
    }
    fdset_zero(&readfds);
    fdset_set(fds[0], &readfds);
    memset(&timeout, 0, sizeof(timeout));
    ret = syscall_dispatch_impl(__NR_pselect6, fds[0] + 1, (long)(uintptr_t)&readfds,
                                0, 0, (long)(uintptr_t)&timeout, 0);
    if (ret != 1 || !fdset_isset(fds[0], &readfds)) {
        result = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }
    result = 0;

out:
    close_if_open(fds[0]);
    close_if_open(fds[1]);
    return result;
}

int readiness_contract_pselect6_mask_blocks_signal_until_pipe_ready_and_restores(void) {
    int fds[2] = {-1, -1};
    struct task *parent = task_current();
    struct task *child = NULL;
    struct pselect_mask_case ctx;
    kernel_thread_t thread;
    int result = -1;

    if (pipe_impl(fds) != 0) {
        return errno;
    }
    child = task_create_child_impl(parent);
    if (!child) {
        result = errno ? errno : ENOMEM;
        goto out;
    }
    pselect_case_init(&ctx, fds[0], child);
    if (kernel_thread_create(&thread, NULL, pselect_mask_thread, &ctx) != 0) {
        result = errno ? errno : EIO;
        goto out_destroy;
    }
    kernel_thread_detach(thread);
    pselect_case_wait_started(&ctx);
    if (signal_generate_task(child, SIGUSR1) != 0 || write_impl(fds[1], "m", 1) != 1) {
        result = errno ? errno : EIO;
        goto out_destroy;
    }
    result = pselect_case_wait_done(&ctx);

out_destroy:
    pselect_case_destroy(&ctx);
    task_unlink_child_impl(parent, child);
    task_put(child);
out:
    close_if_open(fds[0]);
    close_if_open(fds[1]);
    return result;
}

int readiness_contract_proc_and_dev_fds_report_readiness(void) {
    int procfd = -1;
    int nullfd = -1;
    struct pollfd pfds[2];
    int result = -1;

    procfd = open_impl("/proc/self/status", O_RDONLY, 0);
    nullfd = open_impl("/dev/null", O_WRONLY, 0);
    if (procfd < 0 || nullfd < 0) {
        result = errno ? errno : ENOENT;
        goto out;
    }
    memset(pfds, 0, sizeof(pfds));
    pfds[0].fd = procfd;
    pfds[0].events = POLLIN;
    pfds[1].fd = nullfd;
    pfds[1].events = POLLOUT;
    if (poll_impl(pfds, 2, 0) != 2 ||
        (pfds[0].revents & POLLIN) == 0 ||
        (pfds[1].revents & POLLOUT) == 0) {
        result = errno ? errno : EIO;
        goto out;
    }
    result = 0;

out:
    close_if_open(procfd);
    close_if_open(nullfd);
    return result;
}

int readiness_contract_synthetic_dirs_and_dev_zero_report_readiness(void) {
    int proc_dir = -1;
    int dev_dir = -1;
    int zero_fd = -1;
    struct pollfd pfds[3];
    int result = -1;

    proc_dir = open_impl("/proc/self", O_RDONLY | O_DIRECTORY, 0);
    dev_dir = open_impl("/dev", O_RDONLY | O_DIRECTORY, 0);
    zero_fd = open_impl("/dev/zero", O_RDWR, 0);
    if (proc_dir < 0 || dev_dir < 0 || zero_fd < 0) {
        result = errno ? errno : ENOENT;
        goto out;
    }

    memset(pfds, 0, sizeof(pfds));
    pfds[0].fd = proc_dir;
    pfds[0].events = POLLIN;
    pfds[1].fd = dev_dir;
    pfds[1].events = POLLIN;
    pfds[2].fd = zero_fd;
    pfds[2].events = POLLIN | POLLOUT;

    if (poll_impl(pfds, 3, 0) != 3 ||
        (pfds[0].revents & POLLIN) == 0 ||
        (pfds[1].revents & POLLIN) == 0 ||
        (pfds[2].revents & POLLIN) == 0 ||
        (pfds[2].revents & POLLOUT) == 0) {
        result = errno ? errno : EIO;
        goto out;
    }
    result = 0;

out:
    close_if_open(proc_dir);
    close_if_open(dev_dir);
    close_if_open(zero_fd);
    return result;
}

int readiness_contract_poll_pidfd_readable_after_task_exit(void) {
    struct task *parent = task_current();
    struct task *child = NULL;
    struct task *restore;
    struct pollfd pfd;
    struct __kernel_timespec timeout = {0, 0};
    int pidfd = -1;
    long ret;

    if (!parent) {
        errno = ESRCH;
        return -1;
    }

    child = task_create_child_impl(parent);
    if (!child) {
        return -1;
    }

    pidfd = (int)syscall_dispatch_impl(__NR_pidfd_open, child->pid, 0, 0, 0, 0, 0);
    if (pidfd < 0) {
        errno = (int)-pidfd;
        goto out;
    }

    memset(&pfd, 0, sizeof(pfd));
    pfd.fd = pidfd;
    pfd.events = POLLIN;
    ret = syscall_dispatch_impl(__NR_ppoll, (long)(uintptr_t)&pfd, 1, (long)(uintptr_t)&timeout, 0, 0, 0);
    if (ret != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }

    restore = task_current();
    task_set_current(child);
    exit_impl(0);
    task_set_current(restore);

    memset(&pfd, 0, sizeof(pfd));
    pfd.fd = pidfd;
    pfd.events = POLLIN;
    ret = syscall_dispatch_impl(__NR_ppoll, (long)(uintptr_t)&pfd, 1, (long)(uintptr_t)&timeout, 0, 0, 0);
    if (ret != 1 || (pfd.revents & POLLIN) == 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }

    signal_clear_pending_task(parent, SIGCHLD);
    close_if_open(pidfd);
    task_unlink_child_impl(parent, child);
    task_put(child);
    return 0;

out:
    close_if_open(pidfd);
    if (child) {
        task_unlink_child_impl(parent, child);
        task_put(child);
    }
    return -1;
}

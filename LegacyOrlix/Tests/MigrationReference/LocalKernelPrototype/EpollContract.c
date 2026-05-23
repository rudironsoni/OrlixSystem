#include <uapi/asm/ioctls.h>
#include <uapi/asm/unistd.h>
#include <uapi/linux/eventpoll.h>
#include <uapi/linux/fcntl.h>
#include <uapi/linux/pidfd.h>
#include <uapi/linux/poll.h>
#include <uapi/linux/signal.h>
#include <uapi/linux/errno.h>
#include <linux/string.h>

#include <stddef.h>

#include "fs/fcntl.h"
#include "fs/fdtable.h"
#include "fs/eventpoll.h"
#include "fs/namei.h"
#include "fs/open.h"
#include "fs/pipe.h"
#include "fs/read_write.h"
#include "kernel/signal.h"
#include "kernel/task.h"
#include "private/kernel/kthread_state.h"
#include "private/kernel/signal_state.h"
#include "private/kernel/task_state.h"
#include "runtime/syscall.h"

extern int errno;

extern int pty_contract_ioctl(int fd, unsigned long request, ...);
extern int signal_generate_task(struct task *target, int32_t sig);

static int close_if_open(int fd) {
    return fd >= 0 ? close_impl(fd) : 0;
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

static int path_for_fd(const char *prefix, int fd, char *path, size_t path_size) {
    size_t prefix_len = strlen(prefix);
    if (prefix_len >= path_size) {
        errno = ENAMETOOLONG;
        return -1;
    }
    memcpy(path, prefix, prefix_len);
    return append_decimal(path + prefix_len, path_size - prefix_len, fd);
}

static int read_fdinfo_flags(int fd_num, unsigned int *flags_out) {
    char path[64];
    char buf[256];
    int infofd;
    long nread;
    char *flags_line;
    unsigned int flags = 0;
    if (path_for_fd("/proc/self/fdinfo/", fd_num, path, sizeof(path)) != 0) return -1;
    infofd = open_impl(path, O_RDONLY, 0);
    if (infofd < 0) return -1;
    nread = read_impl(infofd, buf, sizeof(buf) - 1);
    close_if_open(infofd);
    if (nread <= 0) {
        errno = ENODATA;
        return -1;
    }
    buf[nread] = '\0';
    flags_line = strstr(buf, "flags:\t0");
    if (!flags_line) {
        errno = ENODATA;
        return -1;
    }
    flags_line += 8;
    while (*flags_line >= '0' && *flags_line <= '7') {
        flags = (flags << 3) | (unsigned int)(*flags_line - '0');
        flags_line++;
    }
    *flags_out = flags;
    return 0;
}

static int alloc_pty_pair(int *master_fd_out, int *slave_fd_out) {
    int master_fd = -1;
    int slave_fd = -1;
    unsigned int pty_index = 0;
    int unlock = 0;
    char slave_path[64];
    master_fd = open_impl("/dev/ptmx", O_RDWR, 0);
    if (master_fd < 0) return -1;
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

struct epoll_thread_case {
    int epfd;
    int write_fd;
    kernel_mutex_t lock;
    kernel_cond_t cond;
    int started;
    int restart_ready;
    int proceed;
    int done;
    int result;
    struct task *task;
};

struct epoll_mask_case {
    int epfd;
    kernel_mutex_t lock;
    kernel_cond_t cond;
    int started;
    int done;
    int result;
    struct task *task;
};

static void case_init(struct epoll_thread_case *ctx, int epfd) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->epfd = epfd;
    kernel_mutex_init(&ctx->lock);
    kernel_cond_init(&ctx->cond);
}

static void case_destroy(struct epoll_thread_case *ctx) {
    kernel_cond_destroy(&ctx->cond);
    kernel_mutex_destroy(&ctx->lock);
}

static void case_mark_started(struct epoll_thread_case *ctx) {
    kernel_mutex_lock(&ctx->lock);
    ctx->started = 1;
    kernel_cond_broadcast(&ctx->cond);
    kernel_mutex_unlock(&ctx->lock);
}

static void case_mark_done(struct epoll_thread_case *ctx, int result) {
    kernel_mutex_lock(&ctx->lock);
    ctx->result = result;
    ctx->done = 1;
    kernel_cond_broadcast(&ctx->cond);
    kernel_mutex_unlock(&ctx->lock);
}

static void case_mark_restart_ready(struct epoll_thread_case *ctx) {
    kernel_mutex_lock(&ctx->lock);
    ctx->restart_ready = 1;
    kernel_cond_broadcast(&ctx->cond);
    while (!ctx->proceed) {
        kernel_cond_wait(&ctx->cond, &ctx->lock);
    }
    kernel_mutex_unlock(&ctx->lock);
}

static void case_wait_restart_ready(struct epoll_thread_case *ctx) {
    kernel_mutex_lock(&ctx->lock);
    while (!ctx->restart_ready) {
        kernel_cond_wait(&ctx->cond, &ctx->lock);
    }
    kernel_mutex_unlock(&ctx->lock);
}

static void case_allow_restart(struct epoll_thread_case *ctx) {
    kernel_mutex_lock(&ctx->lock);
    ctx->proceed = 1;
    kernel_cond_broadcast(&ctx->cond);
    kernel_mutex_unlock(&ctx->lock);
}

static void case_wait_started(struct epoll_thread_case *ctx) {
    kernel_mutex_lock(&ctx->lock);
    while (!ctx->started) {
        kernel_cond_wait(&ctx->cond, &ctx->lock);
    }
    kernel_mutex_unlock(&ctx->lock);
}

static int case_wait_done(struct epoll_thread_case *ctx) {
    int result;
    kernel_mutex_lock(&ctx->lock);
    while (!ctx->done) {
        kernel_cond_wait(&ctx->cond, &ctx->lock);
    }
    result = ctx->result;
    kernel_mutex_unlock(&ctx->lock);
    return result;
}

static void epoll_mask_case_init(struct epoll_mask_case *ctx, int epfd, struct task *task) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->epfd = epfd;
    ctx->task = task;
    kernel_mutex_init(&ctx->lock);
    kernel_cond_init(&ctx->cond);
}

static void epoll_mask_case_destroy(struct epoll_mask_case *ctx) {
    kernel_cond_destroy(&ctx->cond);
    kernel_mutex_destroy(&ctx->lock);
}

static void epoll_mask_case_mark_started(struct epoll_mask_case *ctx) {
    kernel_mutex_lock(&ctx->lock);
    ctx->started = 1;
    kernel_cond_broadcast(&ctx->cond);
    kernel_mutex_unlock(&ctx->lock);
}

int epoll_contract_wait_pidfd_readable_after_task_exit(void) {
    struct task *parent = task_current();
    struct task *child = NULL;
    struct task *restore;
    struct epoll_event ev;
    struct epoll_event events[1];
    int epfd = -1;
    int pidfd = -1;
    int ret;

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

    epfd = epoll_create1_impl(0);
    if (epfd < 0) {
        goto out;
    }

    memset(&ev, 0, sizeof(ev));
    ev.events = EPOLLIN;
    ev.data = 0xdeadbeefU;
    if (epoll_ctl_impl(epfd, EPOLL_CTL_ADD, pidfd, &ev) != 0) {
        goto out;
    }

    memset(events, 0, sizeof(events));
    ret = epoll_wait_impl(epfd, events, 1, 0);
    if (ret != 0) {
        errno = ret < 0 ? errno : EPROTO;
        goto out;
    }

    restore = task_current();
    task_set_current(child);
    exit_impl(0);
    task_set_current(restore);

    memset(events, 0, sizeof(events));
    ret = epoll_wait_impl(epfd, events, 1, 0);
    if (ret != 1 || (events[0].events & EPOLLIN) == 0 || events[0].data != 0xdeadbeefU) {
        errno = ret < 0 ? errno : EPROTO;
        goto out;
    }

    signal_clear_pending_task(parent, SIGCHLD);
    close_if_open(epfd);
    close_if_open(pidfd);
    task_unlink_child_impl(parent, child);
    task_put(child);
    return 0;

out:
    close_if_open(epfd);
    close_if_open(pidfd);
    if (child) {
        task_unlink_child_impl(parent, child);
        task_put(child);
    }
    return -1;
}

int epoll_contract_edge_trigger_reports_once_until_pipe_drained(void) {
    errno = 0;
    int epfd = -1;
    int pipefds[2] = {-1, -1};
    struct epoll_event ev;
    struct epoll_event events[1];
    char byte = 'x';
    long ret;
    int rc;

    if (pipe_impl(pipefds) != 0) {
        errno = 1101;
        return -1;
    }
    epfd = epoll_create1_impl(0);
    if (epfd < 0) {
        errno = 1102;
        goto out;
    }

    memset(&ev, 0, sizeof(ev));
    ev.events = EPOLLIN | EPOLLET;
    ev.data = 0x11111111ULL;
    if (epoll_ctl_impl(epfd, EPOLL_CTL_ADD, pipefds[0], &ev) != 0) {
        errno = 1103;
        goto out;
    }

    ret = write_impl(pipefds[1], &byte, 1);
    if (ret != 1) {
        errno = 1104;
        goto out;
    }

    memset(events, 0, sizeof(events));
    rc = epoll_wait_impl(epfd, events, 1, 0);
    if (rc != 1 || (events[0].events & EPOLLIN) == 0 || events[0].data != 0x11111111ULL) {
        errno = 1105;
        goto out;
    }

    /* Edge-triggered: without draining, no new event should be reported. */
    memset(events, 0, sizeof(events));
    rc = epoll_wait_impl(epfd, events, 1, 0);
    if (rc != 0) {
        errno = 1106;
        goto out;
    }

    ret = read_impl(pipefds[0], &byte, 1);
    if (ret != 1) {
        errno = 1107;
        goto out;
    }

    /* Allow the implementation to observe readiness clearing before the next edge. */
    memset(events, 0, sizeof(events));
    rc = epoll_wait_impl(epfd, events, 1, 0);
    if (rc != 0) {
        errno = 1110;
        goto out;
    }

    ret = write_impl(pipefds[1], &byte, 1);
    if (ret != 1) {
        errno = 1108;
        goto out;
    }

    memset(events, 0, sizeof(events));
    rc = epoll_wait_impl(epfd, events, 1, 0);
    if (rc != 1 || (events[0].events & EPOLLIN) == 0) {
        errno = 1109;
        goto out;
    }

    close_if_open(epfd);
    close_if_open(pipefds[0]);
    close_if_open(pipefds[1]);
    return 0;

out:
    close_if_open(epfd);
    close_if_open(pipefds[0]);
    close_if_open(pipefds[1]);
    return -1;
}

int epoll_contract_oneshot_suppresses_events_until_rearmed(void) {
    int epfd = -1;
    int pipefds[2] = {-1, -1};
    struct epoll_event ev;
    struct epoll_event events[1];
    char byte = 'y';
    long ret;
    int rc;

    if (pipe_impl(pipefds) != 0) {
        return -1;
    }
    epfd = epoll_create1_impl(0);
    if (epfd < 0) {
        goto out;
    }

    memset(&ev, 0, sizeof(ev));
    ev.events = EPOLLIN | EPOLLONESHOT;
    ev.data = 0x22222222ULL;
    if (epoll_ctl_impl(epfd, EPOLL_CTL_ADD, pipefds[0], &ev) != 0) {
        goto out;
    }

    ret = write_impl(pipefds[1], &byte, 1);
    if (ret != 1) {
        errno = ret < 0 ? errno : EPROTO;
        goto out;
    }

    memset(events, 0, sizeof(events));
    rc = epoll_wait_impl(epfd, events, 1, 0);
    if (rc != 1 || (events[0].events & EPOLLIN) == 0 || events[0].data != 0x22222222ULL) {
        errno = rc < 0 ? errno : EPROTO;
        goto out;
    }

    /* Drain. */
    ret = read_impl(pipefds[0], &byte, 1);
    if (ret != 1) {
        errno = ret < 0 ? errno : EPROTO;
        goto out;
    }

    /* Without rearm, oneshot stays disabled. */
    ret = write_impl(pipefds[1], &byte, 1);
    if (ret != 1) {
        errno = ret < 0 ? errno : EPROTO;
        goto out;
    }
    memset(events, 0, sizeof(events));
    rc = epoll_wait_impl(epfd, events, 1, 0);
    if (rc != 0) {
        errno = rc < 0 ? errno : EPROTO;
        goto out;
    }

    /* Rearm via MOD. */
    memset(&ev, 0, sizeof(ev));
    ev.events = EPOLLIN | EPOLLONESHOT;
    ev.data = 0x22222222ULL;
    if (epoll_ctl_impl(epfd, EPOLL_CTL_MOD, pipefds[0], &ev) != 0) {
        goto out;
    }

    /* Drain any queued byte from the suppressed write, then write again. */
    ret = read_impl(pipefds[0], &byte, 1);
    if (ret != 1) {
        errno = ret < 0 ? errno : EPROTO;
        goto out;
    }
    ret = write_impl(pipefds[1], &byte, 1);
    if (ret != 1) {
        errno = ret < 0 ? errno : EPROTO;
        goto out;
    }

    memset(events, 0, sizeof(events));
    rc = epoll_wait_impl(epfd, events, 1, 0);
    if (rc != 1 || (events[0].events & EPOLLIN) == 0) {
        errno = rc < 0 ? errno : EPROTO;
        goto out;
    }

    close_if_open(epfd);
    close_if_open(pipefds[0]);
    close_if_open(pipefds[1]);
    return 0;

out:
    close_if_open(epfd);
    close_if_open(pipefds[0]);
    close_if_open(pipefds[1]);
    return -1;
}

static void epoll_mask_case_mark_done(struct epoll_mask_case *ctx, int result) {
    kernel_mutex_lock(&ctx->lock);
    ctx->result = result;
    ctx->done = 1;
    kernel_cond_broadcast(&ctx->cond);
    kernel_mutex_unlock(&ctx->lock);
}

static void epoll_mask_case_wait_started(struct epoll_mask_case *ctx) {
    kernel_mutex_lock(&ctx->lock);
    while (!ctx->started) {
        kernel_cond_wait(&ctx->cond, &ctx->lock);
    }
    kernel_mutex_unlock(&ctx->lock);
}

static int epoll_mask_case_wait_done(struct epoll_mask_case *ctx) {
    int result;
    kernel_mutex_lock(&ctx->lock);
    while (!ctx->done) {
        kernel_cond_wait(&ctx->cond, &ctx->lock);
    }
    result = ctx->result;
    kernel_mutex_unlock(&ctx->lock);
    return result;
}

static void *epoll_pwait_mask_thread(void *arg) {
    struct epoll_mask_case *ctx = arg;
    struct epoll_event event;
    sigset_t sigmask = {0};
    sigset_t queried = {0};
    long ret;

    task_set_current(ctx->task);
    sigaddset(&sigmask, SIGUSR1);
    epoll_mask_case_mark_started(ctx);
    ret = syscall_dispatch_impl(__NR_epoll_pwait, ctx->epfd, (long)(uintptr_t)&event, 1,
                                -1, (long)(uintptr_t)&sigmask, sizeof(sigmask));
    if (ret != 1 || (event.events & EPOLLIN) == 0) {
        epoll_mask_case_mark_done(ctx, ret < 0 ? (int)-ret : EIO);
        return NULL;
    }
    ret = syscall_dispatch_impl(__NR_rt_sigprocmask, SIG_BLOCK, 0,
                                (long)(uintptr_t)&queried, sizeof(queried), 0, 0);
    if (ret != 0 || sigismember(&queried, SIGUSR1) != 0) {
        epoll_mask_case_mark_done(ctx, ret < 0 ? (int)-ret : EBUSY);
        return NULL;
    }
    epoll_mask_case_mark_done(ctx, 0);
    return NULL;
}

static void *epoll_wait_thread(void *arg) {
    struct epoll_thread_case *ctx = arg;
    struct epoll_event event;
    int ret;
    if (ctx->task) {
        task_set_current(ctx->task);
    }
    case_mark_started(ctx);
    ret = epoll_wait_impl(ctx->epfd, &event, 1, -1);
    if (ret == 1 && (event.events & EPOLLIN)) {
        case_mark_done(ctx, 0);
    } else if (ret == -1 && errno == EINTR) {
        case_mark_done(ctx, EINTR);
    } else {
        case_mark_done(ctx, errno ? errno : EIO);
    }
    return NULL;
}

static void *epoll_restart_thread(void *arg) {
    struct epoll_thread_case *ctx = arg;
    struct epoll_event event;
    long ret;

    task_set_current(ctx->task);
    case_mark_started(ctx);
    ret = epoll_wait_impl(ctx->epfd, &event, 1, -1);
    if (ret != -1 || errno != EINTR ||
        !signal_frame_restart_matches_task(ctx->task, TASK_RESTART_EPOLL_WAIT,
                                           (uint64_t)(int64_t)ctx->epfd,
                                           (uint64_t)(uintptr_t)&event,
                                           1,
                                           (uint64_t)(int64_t)-1,
                                           0, 0)) {
        case_mark_done(ctx, ENODATA);
        return NULL;
    }
    signal_clear_pending_task(ctx->task, SIGUSR1);
    case_mark_restart_ready(ctx);
    ret = syscall_dispatch_impl(__NR_restart_syscall, 0, 0, 0, 0, 0, 0);
    if (ret == 1 && (event.events & EPOLLIN) &&
        signal_frame_restart_is_task(ctx->task, TASK_RESTART_NONE)) {
        case_mark_done(ctx, 0);
    } else {
        case_mark_done(ctx, ret < 0 ? (int)-ret : EIO);
    }
    return NULL;
}

int epoll_contract_create_returns_fd(void) {
    int epfd = epoll_create_impl(1);
    if (epfd < 0) return errno;
    close_if_open(epfd);
    return 0;
}

int epoll_contract_create1_cloexec_sets_fd_cloexec(void) {
    int epfd = epoll_create1_impl(EPOLL_CLOEXEC);
    int ret = 0;
    if (epfd < 0) return errno;
    if (fcntl_impl(epfd, F_GETFD, 0) != FD_CLOEXEC) ret = errno ? errno : EIO;
    close_if_open(epfd);
    return ret;
}

static int add_pipe_read(int epfd, int fd, uint64_t data) {
    struct epoll_event event;
    event.events = EPOLLIN;
    event.data = data;
    return epoll_ctl_impl(epfd, EPOLL_CTL_ADD, fd, &event);
}

int epoll_contract_ctl_add_pipe_read_end(void) {
    int epfd = -1, fds[2] = {-1, -1}, ret = 0;
    epfd = epoll_create1_impl(0);
    if (epfd < 0 || pipe_impl(fds) != 0) return errno;
    if (add_pipe_read(epfd, fds[0], 11) != 0) ret = errno ? errno : EIO;
    close_if_open(epfd); close_if_open(fds[0]); close_if_open(fds[1]);
    return ret;
}

int epoll_contract_ctl_add_duplicate_returns_exist(void) {
    int epfd = -1, fds[2] = {-1, -1}, ret = 0;
    epfd = epoll_create1_impl(0);
    if (epfd < 0 || pipe_impl(fds) != 0) return errno;
    if (add_pipe_read(epfd, fds[0], 11) != 0) {
        ret = errno ? errno : EIO;
        goto out;
    }
    errno = 0;
    if (add_pipe_read(epfd, fds[0], 12) != -1 || errno != EEXIST) ret = errno ? errno : EIO;
out:
    close_if_open(epfd); close_if_open(fds[0]); close_if_open(fds[1]);
    return ret;
}

int epoll_contract_ctl_mod_updates_events(void) {
    int epfd = -1, fds[2] = {-1, -1}, ret = 0;
    struct epoll_event event;
    epfd = epoll_create1_impl(0);
    if (epfd < 0 || pipe_impl(fds) != 0) return errno;
    if (add_pipe_read(epfd, fds[0], 11) != 0) {
        ret = errno ? errno : EIO;
        goto out;
    }
    event.events = EPOLLOUT;
    event.data = 22;
    if (epoll_ctl_impl(epfd, EPOLL_CTL_MOD, fds[0], &event) != 0) ret = errno ? errno : EIO;
out:
    close_if_open(epfd); close_if_open(fds[0]); close_if_open(fds[1]);
    return ret;
}

int epoll_contract_ctl_del_removes_events(void) {
    int epfd = -1, fds[2] = {-1, -1}, ret = 0;
    struct epoll_event event;
    epfd = epoll_create1_impl(0);
    if (epfd < 0 || pipe_impl(fds) != 0) return errno;
    if (add_pipe_read(epfd, fds[0], 11) != 0) {
        ret = errno ? errno : EIO;
        goto out;
    }
    if (epoll_ctl_impl(epfd, EPOLL_CTL_DEL, fds[0], NULL) != 0 ||
        write_impl(fds[1], "x", 1) != 1 ||
        epoll_wait_impl(epfd, &event, 1, 0) != 0) ret = errno ? errno : EIO;
out:
    close_if_open(epfd); close_if_open(fds[0]); close_if_open(fds[1]);
    return ret;
}

int epoll_contract_wait_pipe_readable_after_write(void) {
    int epfd = -1, fds[2] = {-1, -1}, ret = 0;
    struct epoll_event event;
    epfd = epoll_create1_impl(0);
    if (epfd < 0 || pipe_impl(fds) != 0) return errno;
    if (add_pipe_read(epfd, fds[0], 99) != 0 || write_impl(fds[1], "x", 1) != 1) {
        ret = errno ? errno : EIO;
        goto out;
    }
    if (epoll_wait_impl(epfd, &event, 1, 0) != 1 || (event.events & EPOLLIN) == 0 || event.data != 99) ret = errno ? errno : EIO;
out:
    close_if_open(epfd); close_if_open(fds[0]); close_if_open(fds[1]);
    return ret;
}

int epoll_contract_wait_blocks_until_pipe_write(void) {
    int epfd = -1, fds[2] = {-1, -1}, ret = 0;
    struct epoll_thread_case ctx;
    kernel_thread_t thread;
    epfd = epoll_create1_impl(0);
    if (epfd < 0 || pipe_impl(fds) != 0) return errno;
    if (add_pipe_read(epfd, fds[0], 1) != 0) {
        ret = errno ? errno : EIO;
        goto out;
    }
    case_init(&ctx, epfd);
    if (kernel_thread_create(&thread, NULL, epoll_wait_thread, &ctx) != 0) {
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
out:
    close_if_open(epfd); close_if_open(fds[0]); close_if_open(fds[1]);
    return ret;
}

int epoll_contract_wait_timeout_returns_zero(void) {
    int epfd = -1, fds[2] = {-1, -1}, ret = 0;
    struct epoll_event event;
    epfd = epoll_create1_impl(0);
    if (epfd < 0 || pipe_impl(fds) != 0) return errno;
    if (add_pipe_read(epfd, fds[0], 1) != 0) {
        ret = errno ? errno : EIO;
        goto out;
    }
    if (epoll_wait_impl(epfd, &event, 1, 5) != 0) ret = errno ? errno : EIO;
out:
    close_if_open(epfd); close_if_open(fds[0]); close_if_open(fds[1]);
    return ret;
}

int epoll_contract_wait_signal_interrupt_returns_intr(void) {
    int epfd = -1, fds[2] = {-1, -1}, ret = 0;
    struct epoll_thread_case ctx;
    kernel_thread_t thread;
    struct task *parent = task_current();
    struct task *child = NULL;
    epfd = epoll_create1_impl(0);
    if (epfd < 0 || pipe_impl(fds) != 0) return errno;
    child = task_create_child_impl(parent);
    if (!child) {
        ret = errno ? errno : ENOMEM;
        goto out;
    }
    if (add_pipe_read(epfd, fds[0], 1) != 0) {
        ret = errno ? errno : EIO;
        goto out_child;
    }
    case_init(&ctx, epfd);
    ctx.task = child;
    if (kernel_thread_create(&thread, NULL, epoll_wait_thread, &ctx) != 0) {
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
        uint64_t restart_arg2 = 0;
        uint64_t restart_arg3 = 0;
        if (!signal_frame_restart_is_task(child, TASK_RESTART_EPOLL_WAIT) ||
            signal_frame_restart_arg_get_task(child, 0, &restart_arg0) != 0 ||
            signal_frame_restart_arg_get_task(child, 2, &restart_arg2) != 0 ||
            signal_frame_restart_arg_get_task(child, 3, &restart_arg3) != 0 ||
            restart_arg0 != (uint64_t)(int64_t)epfd ||
            restart_arg2 != 1 ||
            (int)restart_arg3 != -1) {
            ret = ENODATA;
        } else {
            signal_frame_restart_clear_task(child);
            ret = 0;
        }
    }
out_destroy:
    case_destroy(&ctx);
out_child:
    task_unlink_child_impl(parent, child);
    task_put(child);
out:
    close_if_open(epfd); close_if_open(fds[0]); close_if_open(fds[1]);
    return ret;
}

int epoll_contract_restart_syscall_reenters_wait(void) {
    int epfd = -1, fds[2] = {-1, -1}, ret = 0;
    struct epoll_thread_case ctx;
    kernel_thread_t thread;
    struct task *parent = task_current();
    struct task *child = NULL;

    epfd = epoll_create1_impl(0);
    if (epfd < 0 || pipe_impl(fds) != 0) return errno;
    child = task_create_child_impl(parent);
    if (!child) {
        ret = errno ? errno : ENOMEM;
        goto out;
    }
    if (add_pipe_read(epfd, fds[0], 1) != 0) {
        ret = errno ? errno : EIO;
        goto out_child;
    }
    case_init(&ctx, epfd);
    ctx.task = child;
    ctx.write_fd = fds[1];
    if (kernel_thread_create(&thread, NULL, epoll_restart_thread, &ctx) != 0) {
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
    if (write_impl(ctx.write_fd, "e", 1) != 1) {
        ret = errno ? errno : EIO;
        case_allow_restart(&ctx);
        goto out_destroy;
    }
    case_allow_restart(&ctx);
    ret = case_wait_done(&ctx);
out_destroy:
    case_destroy(&ctx);
out_child:
    task_unlink_child_impl(parent, child);
    task_put(child);
out:
    close_if_open(epfd); close_if_open(fds[0]); close_if_open(fds[1]);
    return ret;
}

int epoll_contract_wait_reports_pipe_hup_after_writer_close(void) {
    int epfd = -1, fds[2] = {-1, -1}, ret = 0;
    struct epoll_event event;
    epfd = epoll_create1_impl(0);
    if (epfd < 0 || pipe_impl(fds) != 0) return errno;
    if (add_pipe_read(epfd, fds[0], 1) != 0) {
        ret = errno ? errno : EIO;
        goto out;
    }
    close_if_open(fds[1]); fds[1] = -1;
    if (epoll_wait_impl(epfd, &event, 1, 0) != 1 || (event.events & EPOLLHUP) == 0) ret = errno ? errno : EIO;
out:
    close_if_open(epfd); close_if_open(fds[0]); close_if_open(fds[1]);
    return ret;
}

int epoll_contract_wait_pty_readable_after_peer_write(void) {
    int epfd = -1, master = -1, slave = -1, ret = 0;
    struct epoll_event event;
    epfd = epoll_create1_impl(0);
    if (epfd < 0 || alloc_pty_pair(&master, &slave) != 0) return errno;
    if (add_pipe_read(epfd, master, 7) != 0 || write_impl(slave, "x", 1) != 1) {
        ret = errno ? errno : EIO;
        goto out;
    }
    if (epoll_wait_impl(epfd, &event, 1, 0) != 1 || (event.events & EPOLLIN) == 0) ret = errno ? errno : EIO;
out:
    close_if_open(epfd); close_if_open(master); close_if_open(slave);
    return ret;
}

int epoll_contract_fd_appears_in_proc_self_fd(void) {
    int epfd = epoll_create1_impl(0);
    char path[64];
    char target[64];
    int nread;
    int ret = 0;
    if (epfd < 0) return errno;
    if (path_for_fd("/proc/self/fd/", epfd, path, sizeof(path)) != 0) {
        ret = errno;
        goto out;
    }
    nread = readlink_impl(path, target, sizeof(target) - 1);
    if (nread <= 0) {
        ret = errno ? errno : EIO;
        goto out;
    }
    target[nread] = '\0';
    if (strcmp(target, "anon_inode:[eventpoll]") != 0) ret = EIO;
out:
    close_if_open(epfd);
    return ret;
}

int epoll_contract_fdinfo_reports_flags(void) {
    int epfd = epoll_create1_impl(EPOLL_CLOEXEC);
    unsigned int flags = 0;
    int ret = 0;
    if (epfd < 0) return errno;
    if (read_fdinfo_flags(epfd, &flags) != 0) {
        ret = errno;
        goto out;
    }
    if ((flags & O_CLOEXEC) == 0) ret = EIO;
out:
    close_if_open(epfd);
    return ret;
}

int epoll_contract_syscall_surface_wait_pipe_readable(void) {
    int fds[2] = {-1, -1};
    int epfd = -1;
    struct epoll_event event;
    long ret;
    int result = -1;

    if (pipe_impl(fds) != 0) {
        return errno;
    }
    epfd = (int)syscall_dispatch_impl(__NR_epoll_create1, 0, 0, 0, 0, 0, 0);
    if (epfd < 0) {
        result = -epfd;
        goto out;
    }
    memset(&event, 0, sizeof(event));
    event.events = EPOLLIN;
    event.data = 0x5a;
    ret = syscall_dispatch_impl(__NR_epoll_ctl, epfd, EPOLL_CTL_ADD, fds[0],
                                (long)(uintptr_t)&event, 0, 0);
    if (ret != 0) {
        result = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }
    if (write_impl(fds[1], "e", 1) != 1) {
        result = errno ? errno : EIO;
        goto out;
    }
    memset(&event, 0, sizeof(event));
    ret = syscall_dispatch_impl(__NR_epoll_pwait, epfd, (long)(uintptr_t)&event, 1, 0, 0, 0);
    if (ret != 1 || (event.events & EPOLLIN) == 0 || event.data != 0x5a) {
        result = ret < 0 ? (int)-ret : ENODATA;
        goto out;
    }
    result = 0;

out:
    close_if_open(epfd);
    close_if_open(fds[0]);
    close_if_open(fds[1]);
    return result;
}

int epoll_contract_pwait_mask_blocks_signal_until_pipe_ready(void) {
    int fds[2] = {-1, -1};
    int epfd = -1;
    struct task *parent = task_current();
    struct task *child = NULL;
    struct epoll_mask_case ctx;
    kernel_thread_t thread;
    int result = -1;

    epfd = epoll_create1_impl(0);
    if (epfd < 0 || pipe_impl(fds) != 0) {
        result = errno ? errno : EIO;
        goto out;
    }
    if (add_pipe_read(epfd, fds[0], 1) != 0) {
        result = errno ? errno : EIO;
        goto out;
    }
    child = task_create_child_impl(parent);
    if (!child) {
        result = errno ? errno : ENOMEM;
        goto out;
    }
    epoll_mask_case_init(&ctx, epfd, child);
    if (kernel_thread_create(&thread, NULL, epoll_pwait_mask_thread, &ctx) != 0) {
        result = errno ? errno : EIO;
        goto out_destroy;
    }
    kernel_thread_detach(thread);
    epoll_mask_case_wait_started(&ctx);
    if (signal_generate_task(child, SIGUSR1) != 0 || write_impl(fds[1], "s", 1) != 1) {
        result = errno ? errno : EIO;
        goto out_destroy;
    }
    result = epoll_mask_case_wait_done(&ctx);

out_destroy:
    epoll_mask_case_destroy(&ctx);
    task_unlink_child_impl(parent, child);
    task_put(child);
out:
    close_if_open(epfd);
    close_if_open(fds[0]);
    close_if_open(fds[1]);
    return result;
}

int epoll_contract_fdinfo_reports_watched_descriptor(void) {
    int fds[2] = {-1, -1};
    int epfd = -1;
    int infofd = -1;
    char path[64];
    char buf[512];
    long nread;
    int result = -1;

    epfd = epoll_create1_impl(0);
    if (epfd < 0 || pipe_impl(fds) != 0) {
        result = errno ? errno : EIO;
        goto out;
    }
    if (add_pipe_read(epfd, fds[0], 0x66) != 0 ||
        path_for_fd("/proc/self/fdinfo/", epfd, path, sizeof(path)) != 0) {
        result = errno ? errno : EIO;
        goto out;
    }
    infofd = open_impl(path, O_RDONLY, 0);
    if (infofd < 0) {
        result = errno ? errno : ENOENT;
        goto out;
    }
    nread = read_impl(infofd, buf, sizeof(buf) - 1);
    if (nread <= 0) {
        result = errno ? errno : ENODATA;
        goto out;
    }
    buf[nread] = '\0';
    if (!strstr(buf, "tfd:") || !strstr(buf, "events:")) {
        result = ENODATA;
        goto out;
    }
    result = 0;

out:
    close_if_open(infofd);
    close_if_open(epfd);
    close_if_open(fds[0]);
    close_if_open(fds[1]);
    return result;
}

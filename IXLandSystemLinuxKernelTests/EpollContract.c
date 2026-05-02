#include <asm/ioctls.h>
#include <asm/unistd.h>
#include <linux/eventpoll.h>
#include <linux/fcntl.h>
#include <linux/poll.h>

#ifdef SIGUSR1
#undef SIGUSR1
#endif
#define __ASSEMBLY__ 1
#include <asm-generic/signal.h>
#undef __ASSEMBLY__

#include <errno.h>
#include <stddef.h>
#include <string.h>

#include "fs/fdtable.h"
#include "kernel/signal.h"
#include "kernel/task.h"
#include "runtime/syscall.h"

extern int ixland_test_ioctl(int fd, unsigned long request, ...);
extern int open_impl(const char *pathname, int flags, linux_mode_t mode);
extern int pipe_impl(int pipefd[2]);
extern int fcntl_impl(int fd, int cmd, ...);
extern long write_impl(int fd, const void *buf, size_t count);
extern long read_impl(int fd, void *buf, size_t count);
extern int readlink_impl(const char *pathname, char *buf, size_t bufsiz);
extern int epoll_create_impl(int size);
extern int epoll_create1_impl(int flags);
extern int epoll_ctl_impl(int epfd, int op, int fd, struct epoll_event *event);
extern int epoll_wait_impl(int epfd, struct epoll_event *events, int maxevents, int timeout);
extern int signal_generate_task(struct task_struct *target, int32_t sig);

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
    if (ixland_test_ioctl(master_fd, TIOCGPTN, &pty_index) != 0) {
        close_impl(master_fd);
        return -1;
    }
    if (ixland_test_ioctl(master_fd, TIOCSPTLCK, &unlock) != 0) {
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
    kernel_mutex_t lock;
    kernel_cond_t cond;
    int started;
    int done;
    int result;
    struct task_struct *task;
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

static void *epoll_wait_thread(void *arg) {
    struct epoll_thread_case *ctx = arg;
    struct epoll_event event;
    int ret;
    if (ctx->task) {
        set_current(ctx->task);
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
    struct task_struct *parent = get_current();
    struct task_struct *child = NULL;
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
    if (ret == EINTR) ret = 0;
out_destroy:
    case_destroy(&ctx);
out_child:
    task_unlink_child_impl(parent, child);
    free_task(child);
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

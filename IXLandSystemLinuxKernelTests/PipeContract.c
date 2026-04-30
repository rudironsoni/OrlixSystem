#include <linux/fcntl.h>
#include <linux/poll.h>
#include <linux/stat.h>
#ifdef SIGPIPE
#undef SIGPIPE
#endif
#ifdef SIGUSR1
#undef SIGUSR1
#endif
#define __ASSEMBLY__ 1
#include <asm-generic/signal.h>
#undef __ASSEMBLY__

#include <errno.h>
#include <poll.h>
#include <stddef.h>
#include <string.h>

#include "fs/fdtable.h"
#include "fs/vfs.h"
#include "kernel/signal.h"
#include "kernel/task.h"

extern int pipe_impl(int pipefd[2]);
extern int pipe2_impl(int pipefd[2], int flags);
extern int open_impl(const char *pathname, int flags, linux_mode_t mode);
extern int dup_impl(int oldfd);
extern int fcntl_impl(int fd, int cmd, ...);
extern long read_impl(int fd, void *buf, size_t count);
extern long write_impl(int fd, const void *buf, size_t count);
extern linux_off_t lseek_impl(int fd, linux_off_t offset, int whence);
extern ssize_t pread_impl(int fd, void *buf, size_t count, linux_off_t offset);
extern ssize_t pwrite_impl(int fd, const void *buf, size_t count, linux_off_t offset);
extern int fstat_impl(int fd, struct linux_stat *statbuf);
extern ssize_t getdents64(int fd, void *dirp, size_t count);
extern int poll_impl(struct pollfd *fds, nfds_t nfds, int timeout);
extern int readlink_impl(const char *pathname, char *buf, size_t bufsiz);
extern int signal_generate_task(struct task_struct *target, int32_t sig);

#ifndef SEEK_SET
#define SEEK_SET 0
#endif

static int close_if_open(int fd) {
    if (fd >= 0) {
        return close_impl(fd);
    }
    return 0;
}

static int append_decimal(char *buf, size_t buf_size, int value) {
    char digits[16];
    size_t count = 0;
    size_t i;

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

    for (i = 0; i < count; i++) {
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

    if (path_for_fd("/proc/self/fdinfo/", fd_num, path, sizeof(path)) != 0) {
        return -1;
    }

    infofd = open_impl(path, O_RDONLY, 0);
    if (infofd < 0) {
        return -1;
    }

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

static void clear_pending_signal(struct task_struct *task, int sig) {
    if (!task || !task->signal || sig < 1 || sig > KERNEL_SIG_NUM) {
        return;
    }
    task->signal->pending.sig[(sig - 1) >> 6] &= ~(1ULL << ((sig - 1) & 63));
}

struct pipe_thread_case {
    int fds[2];
    kernel_mutex_t lock;
    kernel_cond_t cond;
    int started;
    int done;
    int result;
    struct task_struct *task;
};

static void case_init(struct pipe_thread_case *ctx, int read_fd, int write_fd) {
    ctx->fds[0] = read_fd;
    ctx->fds[1] = write_fd;
    ctx->started = 0;
    ctx->done = 0;
    ctx->result = 0;
    ctx->task = NULL;
    kernel_mutex_init(&ctx->lock);
    kernel_cond_init(&ctx->cond);
}

static void case_destroy(struct pipe_thread_case *ctx) {
    kernel_cond_destroy(&ctx->cond);
    kernel_mutex_destroy(&ctx->lock);
}

static void case_mark_started(struct pipe_thread_case *ctx) {
    kernel_mutex_lock(&ctx->lock);
    ctx->started = 1;
    kernel_cond_broadcast(&ctx->cond);
    kernel_mutex_unlock(&ctx->lock);
}

static void case_mark_done(struct pipe_thread_case *ctx, int result) {
    kernel_mutex_lock(&ctx->lock);
    ctx->result = result;
    ctx->done = 1;
    kernel_cond_broadcast(&ctx->cond);
    kernel_mutex_unlock(&ctx->lock);
}

static void case_wait_started(struct pipe_thread_case *ctx) {
    kernel_mutex_lock(&ctx->lock);
    while (!ctx->started) {
        kernel_cond_wait(&ctx->cond, &ctx->lock);
    }
    kernel_mutex_unlock(&ctx->lock);
}

static int case_wait_done(struct pipe_thread_case *ctx) {
    int result;

    kernel_mutex_lock(&ctx->lock);
    while (!ctx->done) {
        kernel_cond_wait(&ctx->cond, &ctx->lock);
    }
    result = ctx->result;
    kernel_mutex_unlock(&ctx->lock);
    return result;
}

static void *blocking_read_thread(void *arg) {
    struct pipe_thread_case *ctx = arg;
    char byte = 0;
    long nread;

    if (ctx->task) {
        set_current(ctx->task);
    }

    case_mark_started(ctx);
    nread = read_impl(ctx->fds[0], &byte, 1);
    if (nread == 1 && byte == 'q') {
        case_mark_done(ctx, 0);
    } else if (nread == -1 && errno == EINTR) {
        case_mark_done(ctx, EINTR);
    } else {
        case_mark_done(ctx, errno ? errno : EIO);
    }
    return NULL;
}

static void *blocking_write_thread(void *arg) {
    struct pipe_thread_case *ctx = arg;
    char byte = 'w';
    long nwritten;

    case_mark_started(ctx);
    nwritten = write_impl(ctx->fds[1], &byte, 1);
    if (nwritten == 1) {
        case_mark_done(ctx, 0);
    } else {
        case_mark_done(ctx, errno ? errno : EIO);
    }
    return NULL;
}

int pipe_contract_rejects_null_pipefd(void) {
    errno = 0;
    if (pipe_impl(NULL) != -1 || errno != EFAULT) {
        return errno ? errno : EIO;
    }
    return 0;
}

int pipe_contract_allocates_lowest_available_descriptors(void) {
    int fds[2] = {-1, -1};
    if (pipe_impl(fds) != 0) {
        return errno;
    }
    if (fds[0] != 3 || fds[1] != 4) {
        close_if_open(fds[0]);
        close_if_open(fds[1]);
        errno = EBUSY;
        return errno;
    }
    close_if_open(fds[0]);
    close_if_open(fds[1]);
    return 0;
}

int pipe_contract_read_end_is_readable_only(void) {
    int fds[2] = {-1, -1};
    char byte = 'x';
    int ret = 0;

    if (pipe_impl(fds) != 0) {
        return errno;
    }
    if (write_impl(fds[0], &byte, 1) != -1 || errno != EBADF) {
        ret = errno ? errno : EIO;
    }
    close_if_open(fds[0]);
    close_if_open(fds[1]);
    return ret;
}

int pipe_contract_write_end_is_writable_only(void) {
    int fds[2] = {-1, -1};
    char byte;
    int ret = 0;

    if (pipe_impl(fds) != 0) {
        return errno;
    }
    if (read_impl(fds[1], &byte, 1) != -1 || errno != EBADF) {
        ret = errno ? errno : EIO;
    }
    close_if_open(fds[0]);
    close_if_open(fds[1]);
    return ret;
}

int pipe_contract_round_trip_read_write(void) {
    int fds[2] = {-1, -1};
    char buf[6] = {0};
    int ret = 0;

    if (pipe_impl(fds) != 0) {
        return errno;
    }
    if (write_impl(fds[1], "hello", 5) != 5 ||
        read_impl(fds[0], buf, 5) != 5 ||
        memcmp(buf, "hello", 5) != 0) {
        ret = errno ? errno : EIO;
    }
    close_if_open(fds[0]);
    close_if_open(fds[1]);
    return ret;
}

int pipe_contract_read_consumes_bytes_in_order(void) {
    int fds[2] = {-1, -1};
    char first[3] = {0};
    char second[4] = {0};
    int ret = 0;

    if (pipe_impl(fds) != 0) {
        return errno;
    }
    if (write_impl(fds[1], "abcdef", 6) != 6 ||
        read_impl(fds[0], first, 2) != 2 ||
        read_impl(fds[0], second, 3) != 3 ||
        memcmp(first, "ab", 2) != 0 ||
        memcmp(second, "cde", 3) != 0) {
        ret = errno ? errno : EIO;
    }
    close_if_open(fds[0]);
    close_if_open(fds[1]);
    return ret;
}

int pipe_contract_read_empty_no_writers_returns_eof(void) {
    int fds[2] = {-1, -1};
    char byte;
    int ret = 0;

    if (pipe_impl(fds) != 0) {
        return errno;
    }
    close_if_open(fds[1]);
    fds[1] = -1;
    if (read_impl(fds[0], &byte, 1) != 0) {
        ret = errno ? errno : EIO;
    }
    close_if_open(fds[0]);
    return ret;
}

int pipe_contract_read_empty_nonblocking_returns_again(void) {
    int fds[2] = {-1, -1};
    char byte;
    int ret = 0;

    if (pipe2_impl(fds, O_NONBLOCK) != 0) {
        return errno;
    }
    if (read_impl(fds[0], &byte, 1) != -1 || errno != EAGAIN) {
        ret = errno ? errno : EIO;
    }
    close_if_open(fds[0]);
    close_if_open(fds[1]);
    return ret;
}

int pipe_contract_write_no_readers_returns_pipe(void) {
    int fds[2] = {-1, -1};
    char byte = 'x';
    int ret = 0;

    if (pipe_impl(fds) != 0) {
        return errno;
    }
    close_if_open(fds[0]);
    fds[0] = -1;
    if (write_impl(fds[1], &byte, 1) != -1 || errno != EPIPE) {
        ret = errno ? errno : EIO;
    }
    close_if_open(fds[1]);
    return ret;
}

int pipe_contract_pipe2_cloexec_sets_both_descriptors(void) {
    int fds[2] = {-1, -1};
    int ret = 0;

    if (pipe2_impl(fds, O_CLOEXEC) != 0) {
        return errno;
    }
    if (fcntl_impl(fds[0], F_GETFD, 0) != FD_CLOEXEC ||
        fcntl_impl(fds[1], F_GETFD, 0) != FD_CLOEXEC) {
        ret = errno ? errno : EIO;
    }
    close_if_open(fds[0]);
    close_if_open(fds[1]);
    return ret;
}

int pipe_contract_pipe2_nonblock_sets_both_descriptors(void) {
    int fds[2] = {-1, -1};
    int ret = 0;

    if (pipe2_impl(fds, O_NONBLOCK) != 0) {
        return errno;
    }
    if ((fcntl_impl(fds[0], F_GETFL, 0) & O_NONBLOCK) == 0 ||
        (fcntl_impl(fds[1], F_GETFL, 0) & O_NONBLOCK) == 0) {
        ret = errno ? errno : EIO;
    }
    close_if_open(fds[0]);
    close_if_open(fds[1]);
    return ret;
}

int pipe_contract_pipe2_rejects_unsupported_flags(void) {
    int fds[2] = {-1, -1};
    errno = 0;
    if (pipe2_impl(fds, O_APPEND) != -1 || errno != EINVAL) {
        return errno ? errno : EIO;
    }
    return 0;
}

int pipe_contract_dup_shares_pipe_object(void) {
    int fds[2] = {-1, -1};
    int dupfd = -1;
    char byte = 0;
    int ret = 0;

    if (pipe_impl(fds) != 0) {
        return errno;
    }
    dupfd = dup_impl(fds[0]);
    if (dupfd < 0) {
        ret = errno;
        goto out;
    }
    close_if_open(fds[0]);
    fds[0] = -1;
    if (write_impl(fds[1], "z", 1) != 1 ||
        read_impl(dupfd, &byte, 1) != 1 ||
        byte != 'z') {
        ret = errno ? errno : EIO;
    }
out:
    close_if_open(fds[0]);
    close_if_open(fds[1]);
    close_if_open(dupfd);
    return ret;
}

int pipe_contract_close_on_exec_closes_only_flagged_pipe_descriptor(void) {
    int fds[2] = {-1, -1};
    int ret = 0;

    if (pipe_impl(fds) != 0) {
        return errno;
    }
    if (fcntl_impl(fds[0], F_SETFD, FD_CLOEXEC) != 0) {
        ret = errno;
        goto out;
    }
    if (close_on_exec_impl() != 1 ||
        fdtable_is_used_impl(fds[0]) ||
        !fdtable_is_used_impl(fds[1])) {
        ret = errno ? errno : EIO;
    }
out:
    close_if_open(fds[0]);
    close_if_open(fds[1]);
    return ret;
}

int pipe_contract_fstat_reports_fifo(void) {
    int fds[2] = {-1, -1};
    struct linux_stat st;
    int ret = 0;

    if (pipe_impl(fds) != 0) {
        return errno;
    }
    if (fstat_impl(fds[0], &st) != 0 || (st.st_mode & S_IFMT) != S_IFIFO) {
        ret = errno ? errno : EIO;
    }
    close_if_open(fds[0]);
    close_if_open(fds[1]);
    return ret;
}

int pipe_contract_lseek_returns_spipe(void) {
    int fds[2] = {-1, -1};
    int ret = 0;
    if (pipe_impl(fds) != 0) return errno;
    if (lseek_impl(fds[0], 0, SEEK_SET) != -1 || errno != ESPIPE) ret = errno ? errno : EIO;
    close_if_open(fds[0]);
    close_if_open(fds[1]);
    return ret;
}

int pipe_contract_pread_returns_spipe(void) {
    int fds[2] = {-1, -1};
    char byte;
    int ret = 0;
    if (pipe_impl(fds) != 0) return errno;
    if (pread_impl(fds[0], &byte, 1, 0) != -1 || errno != ESPIPE) ret = errno ? errno : EIO;
    close_if_open(fds[0]);
    close_if_open(fds[1]);
    return ret;
}

int pipe_contract_pwrite_returns_spipe(void) {
    int fds[2] = {-1, -1};
    char byte = 'x';
    int ret = 0;
    if (pipe_impl(fds) != 0) return errno;
    if (pwrite_impl(fds[1], &byte, 1, 0) != -1 || errno != ESPIPE) ret = errno ? errno : EIO;
    close_if_open(fds[0]);
    close_if_open(fds[1]);
    return ret;
}

int pipe_contract_getdents_returns_notdir(void) {
    int fds[2] = {-1, -1};
    char buf[128];
    int ret = 0;
    if (pipe_impl(fds) != 0) return errno;
    if (getdents64(fds[0], buf, sizeof(buf)) != -1 || errno != ENOTDIR) ret = errno ? errno : EIO;
    close_if_open(fds[0]);
    close_if_open(fds[1]);
    return ret;
}

int pipe_contract_poll_read_end_readable_when_data_available(void) {
    int fds[2] = {-1, -1};
    struct pollfd pfd;
    int ret = 0;
    if (pipe_impl(fds) != 0) return errno;
    if (write_impl(fds[1], "x", 1) != 1) {
        ret = errno;
        goto out;
    }
    pfd.fd = fds[0];
    pfd.events = POLLIN;
    pfd.revents = 0;
    if (poll_impl(&pfd, 1, 0) != 1 || (pfd.revents & POLLIN) == 0) ret = errno ? errno : EIO;
out:
    close_if_open(fds[0]);
    close_if_open(fds[1]);
    return ret;
}

int pipe_contract_poll_read_end_hup_when_no_writers(void) {
    int fds[2] = {-1, -1};
    struct pollfd pfd;
    int ret = 0;
    if (pipe_impl(fds) != 0) return errno;
    close_if_open(fds[1]);
    fds[1] = -1;
    pfd.fd = fds[0];
    pfd.events = POLLIN;
    pfd.revents = 0;
    if (poll_impl(&pfd, 1, 0) != 1 || (pfd.revents & POLLHUP) == 0) ret = errno ? errno : EIO;
    close_if_open(fds[0]);
    return ret;
}

int pipe_contract_poll_write_end_writable_when_capacity_available(void) {
    int fds[2] = {-1, -1};
    struct pollfd pfd;
    int ret = 0;
    if (pipe_impl(fds) != 0) return errno;
    pfd.fd = fds[1];
    pfd.events = POLLOUT;
    pfd.revents = 0;
    if (poll_impl(&pfd, 1, 0) != 1 || (pfd.revents & POLLOUT) == 0) ret = errno ? errno : EIO;
    close_if_open(fds[0]);
    close_if_open(fds[1]);
    return ret;
}

int pipe_contract_poll_invalid_fd_returns_nval(void) {
    struct pollfd pfd;
    pfd.fd = NR_OPEN_DEFAULT + 1;
    pfd.events = POLLIN;
    pfd.revents = 0;
    if (poll_impl(&pfd, 1, 0) != 1 || (pfd.revents & POLLNVAL) == 0) {
        return errno ? errno : EIO;
    }
    return 0;
}

int pipe_contract_proc_self_fd_shows_pipe_descriptor(void) {
    int fds[2] = {-1, -1};
    char path[64];
    char target[64];
    int nread;
    int ret = 0;

    if (pipe_impl(fds) != 0) {
        return errno;
    }
    if (path_for_fd("/proc/self/fd/", fds[0], path, sizeof(path)) != 0) {
        ret = errno;
        goto out;
    }
    nread = readlink_impl(path, target, sizeof(target) - 1);
    if (nread <= 0) {
        ret = errno ? errno : EIO;
        goto out;
    }
    target[nread] = '\0';
    if (strncmp(target, "pipe:[", 6) != 0) {
        ret = EIO;
    }
out:
    close_if_open(fds[0]);
    close_if_open(fds[1]);
    return ret;
}

int pipe_contract_proc_self_fdinfo_shows_pipe_flags(void) {
    int fds[2] = {-1, -1};
    unsigned int flags = 0;
    int ret = 0;

    if (pipe2_impl(fds, O_NONBLOCK | O_CLOEXEC) != 0) {
        return errno;
    }
    if (read_fdinfo_flags(fds[0], &flags) != 0) {
        ret = errno;
        goto out;
    }
    if ((flags & O_NONBLOCK) == 0 || (flags & O_CLOEXEC) == 0) {
        ret = EIO;
    }
out:
    close_if_open(fds[0]);
    close_if_open(fds[1]);
    return ret;
}

int pipe_contract_blocking_read_wakes_when_writer_writes(void) {
    int fds[2] = {-1, -1};
    struct pipe_thread_case ctx;
    kernel_thread_t thread;
    int ret = 0;

    if (pipe_impl(fds) != 0) {
        return errno;
    }

    case_init(&ctx, fds[0], fds[1]);
    if (kernel_thread_create(&thread, NULL, blocking_read_thread, &ctx) != 0) {
        ret = errno ? errno : EIO;
        goto out_destroy;
    }
    kernel_thread_detach(thread);
    case_wait_started(&ctx);

    if (write_impl(fds[1], "q", 1) != 1) {
        ret = errno ? errno : EIO;
        goto out_destroy;
    }

    ret = case_wait_done(&ctx);

out_destroy:
    case_destroy(&ctx);
    close_if_open(fds[0]);
    close_if_open(fds[1]);
    return ret;
}

int pipe_contract_blocking_read_interrupted_by_signal(void) {
    int fds[2] = {-1, -1};
    struct pipe_thread_case ctx;
    kernel_thread_t thread;
    struct task_struct *parent = get_current();
    struct task_struct *child = NULL;
    int ret = 0;

    if (pipe_impl(fds) != 0) {
        return errno;
    }
    if (!parent) {
        ret = ESRCH;
        goto out_close;
    }

    child = task_create_child_impl(parent);
    if (!child) {
        ret = errno ? errno : ENOMEM;
        goto out_close;
    }

    case_init(&ctx, fds[0], fds[1]);
    ctx.task = child;
    if (kernel_thread_create(&thread, NULL, blocking_read_thread, &ctx) != 0) {
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
        ret = 0;
    }

out_destroy:
    case_destroy(&ctx);
    if (child) {
        task_unlink_child_impl(parent, child);
        free_task(child);
    }
out_close:
    close_if_open(fds[0]);
    close_if_open(fds[1]);
    return ret;
}

int pipe_contract_blocking_write_wakes_when_reader_drains(void) {
    int fds[2] = {-1, -1};
    struct pipe_thread_case ctx;
    kernel_thread_t thread;
    char fill[4096];
    char byte;
    int ret = 0;

    memset(fill, 'f', sizeof(fill));
    if (pipe2_impl(fds, O_NONBLOCK) != 0) {
        return errno;
    }
    while (write_impl(fds[1], fill, sizeof(fill)) > 0) {
    }
    if (errno != EAGAIN) {
        ret = errno ? errno : EIO;
        goto out_close;
    }
    if (fcntl_impl(fds[1], F_SETFL, 0) != 0) {
        ret = errno ? errno : EIO;
        goto out_close;
    }

    case_init(&ctx, fds[0], fds[1]);
    if (kernel_thread_create(&thread, NULL, blocking_write_thread, &ctx) != 0) {
        ret = errno ? errno : EIO;
        goto out_destroy;
    }
    kernel_thread_detach(thread);
    case_wait_started(&ctx);

    if (read_impl(fds[0], &byte, 1) != 1) {
        ret = errno ? errno : EIO;
        goto out_destroy;
    }

    ret = case_wait_done(&ctx);

out_destroy:
    case_destroy(&ctx);
out_close:
    close_if_open(fds[0]);
    close_if_open(fds[1]);
    return ret;
}

int pipe_contract_write_no_readers_queues_sigpipe(void) {
    int fds[2] = {-1, -1};
    struct task_struct *task = get_current();
    char byte = 'x';
    int ret = 0;

    if (!task) {
        return ESRCH;
    }
    clear_pending_signal(task, SIGPIPE);
    if (pipe_impl(fds) != 0) {
        return errno;
    }

    close_if_open(fds[0]);
    fds[0] = -1;
    if (write_impl(fds[1], &byte, 1) != -1 || errno != EPIPE) {
        ret = errno ? errno : EIO;
        goto out;
    }
    if (!signal_is_pending(task, SIGPIPE)) {
        ret = ENODATA;
    }
    clear_pending_signal(task, SIGPIPE);

out:
    close_if_open(fds[1]);
    return ret;
}

#include <uapi/asm/ioctls.h>
#include <uapi/asm/unistd.h>
#include <uapi/linux/errno.h>
#include <uapi/linux/fcntl.h>
#include <uapi/linux/sched.h>
#include <uapi/linux/signal.h>
#include <uapi/linux/wait.h>
#include <linux/string.h>
#include <uapi/asm/siginfo.h>

#include "../../kunit/kunit.h"
#include "../../kunit/suite_registry.h"
#include "kernel/init.h"
#include "fs/fdtable.h"
#include "fs/pty.h"
#include "runtime/syscall.h"
#include "kernel/signal.h"
#include "kernel/task.h"
#include "kernel/wait.h"

extern int open_impl(const char *pathname, int flags, uint32_t mode);
extern int close_impl(int fd);
extern long read_impl(int fd, void *buf, size_t count);
extern long write_impl(int fd, const void *buf, size_t count);
extern int pty_contract_ioctl(int fd, unsigned long request, ...);
extern int32_t clone_impl(uint64_t flags);
extern __kernel_pid_t kernel_waitpid(__kernel_pid_t pid, int *wstatus, int options)
    __asm("_waitpid");
extern __kernel_pid_t kernel_wait4(__kernel_pid_t pid, int *wstatus, int options, void *rusage)
    __asm("_wait4");
extern int kernel_waitid(int idtype, __kernel_pid_t id, siginfo_t *infop, int options)
    __asm("_waitid");
extern int errno;

static int wait_status_exited(int status) {
    return (status & 0x7f) == 0;
}

static int wait_status_exit_code(int status) {
    return (status >> 8) & 0xff;
}

static int wait_status_stopped(int status) {
    return (status & 0xff) == 0x7f;
}

static int wait_status_stop_signal(int status) {
    return (status >> 8) & 0xff;
}

static int wait_status_continued(int status) {
    return status == 0xffff;
}

static int close_if_open(int fd) {
    if (fd >= 0) {
        return close_impl(fd);
    }
    return 0;
}

static void clear_pending_signal(struct task *task, int32_t sig) {
    if (!task || !task->signal || sig < 1 || sig > KERNEL_SIG_NUM) {
        return;
    }
    task->thread_pending_signals &= ~(1ULL << ((sig - 1) & 63));
    task->signal->shared_pending.sig[(sig - 1) >> 6] &= ~(1ULL << ((sig - 1) & 63));
}

static void reset_wait_job_control_test_kernel_state(void) {
    struct task *child;

    start_kernel();
    if (!kernel_is_booted() || !task_init_process) {
        return;
    }

    task_set_current(task_init_process);
    task_init_process->parent = NULL;
    task_init_process->ppid = 0;
    task_init_process->pgid = task_init_process->pid;
    task_init_process->sid = task_init_process->pid;
    task_init_process->exit_status = 0;
    task_init_process->thread_pending_signals = 0;
    atomic_set(&task_init_process->exited, 0);
    atomic_set(&task_init_process->signaled, 0);
    atomic_set(&task_init_process->termsig, 0);
    atomic_set(&task_init_process->stopped, 0);
    atomic_set(&task_init_process->state, RUN_STATE_RUNNING);
    atomic_set(&task_init_process->continued, 0);
    atomic_set(&task_init_process->stop_report_pending, 0);
    atomic_set(&task_init_process->continue_report_pending, 0);
    if (task_init_process->signal) {
        memset(&task_init_process->signal->pending, 0, sizeof(task_init_process->signal->pending));
        memset(&task_init_process->signal->shared_pending, 0, sizeof(task_init_process->signal->shared_pending));
    }

    while ((child = task_init_process->children) != NULL) {
        task_unlink_child_impl(task_init_process, child);
        child->parent = NULL;
        child->ppid = 0;
        task_put(child);
    }
}

struct waitpid_restart_thread {
    struct task *task;
    int32_t child_pid;
    int status;
    int32_t result;
    int saved_errno;
    kernel_mutex_t lock;
    kernel_cond_t cond;
    int started;
    int done;
};

static void waitpid_restart_thread_init(struct waitpid_restart_thread *ctx) {
    memset(ctx, 0, sizeof(*ctx));
    kernel_mutex_init(&ctx->lock);
    kernel_cond_init(&ctx->cond);
}

static void waitpid_restart_thread_destroy(struct waitpid_restart_thread *ctx) {
    kernel_cond_destroy(&ctx->cond);
    kernel_mutex_destroy(&ctx->lock);
}

static void waitpid_restart_thread_mark_started(struct waitpid_restart_thread *ctx) {
    kernel_mutex_lock(&ctx->lock);
    ctx->started = 1;
    kernel_cond_broadcast(&ctx->cond);
    kernel_mutex_unlock(&ctx->lock);
}

static void waitpid_restart_thread_mark_done(struct waitpid_restart_thread *ctx) {
    kernel_mutex_lock(&ctx->lock);
    ctx->done = 1;
    kernel_cond_broadcast(&ctx->cond);
    kernel_mutex_unlock(&ctx->lock);
}

static void waitpid_restart_thread_wait_started(struct waitpid_restart_thread *ctx) {
    kernel_mutex_lock(&ctx->lock);
    while (!ctx->started) {
        kernel_cond_wait(&ctx->cond, &ctx->lock);
    }
    kernel_mutex_unlock(&ctx->lock);
}

static void waitpid_restart_thread_wait_done(struct waitpid_restart_thread *ctx) {
    kernel_mutex_lock(&ctx->lock);
    while (!ctx->done) {
        kernel_cond_wait(&ctx->cond, &ctx->lock);
    }
    kernel_mutex_unlock(&ctx->lock);
}

static void *waitpid_restart_thread_main(void *arg) {
    struct waitpid_restart_thread *ctx = arg;

    task_set_current(ctx->task);
    waitpid_restart_thread_mark_started(ctx);
    ctx->result = waitpid_impl(ctx->child_pid, &ctx->status, 0);
    ctx->saved_errno = errno;
    waitpid_restart_thread_mark_done(ctx);
    return NULL;
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

static int alloc_pty_pair(int *master_fd_out, int *slave_fd_out, unsigned int *pty_index_out) {
    int master_fd = -1;
    int slave_fd = -1;
    unsigned int pty_index = 0;
    int unlock = 0;
    char slave_path[64];

    if (!master_fd_out || !slave_fd_out || !pty_index_out) {
        errno = EINVAL;
        return -1;
    }

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
    *pty_index_out = pty_index;
    return 0;
}

static int detach_controlling_tty_if_present(void) {
    int tty_fd = open_impl("/dev/tty", O_RDWR, 0);

    if (tty_fd < 0) {
        if (errno == ENXIO || errno == EIO) {
            return 0;
        }
        return -1;
    }

    if (pty_contract_ioctl(tty_fd, TIOCNOTTY, 0) != 0) {
        close_impl(tty_fd);
        return -1;
    }

    return close_impl(tty_fd);
}

static struct task *create_child_task(struct task *parent, int own_pgrp) {
    struct task *child;

    child = task_create_child_impl(parent);
    if (!child) {
        return NULL;
    }

    if (own_pgrp && setpgid_impl(child->pid, child->pid) != 0) {
        task_unlink_child_impl(parent, child);
        task_put(child);
        return NULL;
    }

    return child;
}

static void destroy_child_task(struct task *parent, struct task *child) {
    if (!child) {
        return;
    }
    if (parent) {
        task_unlink_child_impl(parent, child);
    }
    task_put(child);
}

static int stop_and_wait_status(struct task *parent, struct task *child, int32_t sig, int *status_out) {
    struct task *cursor;
    int status = 0;
    int32_t waited;

    clear_pending_signal(parent, SIGCHLD);
    if (signal_generate_task(child, sig) != 0) {
        errno = ESRCH;
        return -1;
    }
    if (!atomic_read(&child->stop_report_pending) || !atomic_read(&child->stopped)) {
        errno = ENODATA;
        return -1;
    }
    cursor = parent->children;
    while (cursor && cursor != child) {
        cursor = cursor->next_sibling;
    }
    if (!cursor) {
        errno = ENXIO;
        return -1;
    }
    waited = waitpid_impl(child->pid, &status, WUNTRACED);
    if (waited != child->pid) {
        errno = EBUSY;
        return -1;
    }
    if (!wait_status_stopped(status) || wait_status_stop_signal(status) != sig) {
        errno = ERANGE;
        return -1;
    }
    if (!signal_is_pending(parent, SIGCHLD)) {
        errno = ENOMSG;
        return -1;
    }
    if (status_out) {
        *status_out = status;
    }
    return 0;
}

static int exit_child_with_status(struct task *parent, struct task *child, int exit_status) {
    struct task *saved_current = task_current();
    struct task *cursor;
    clear_pending_signal(parent, SIGCHLD);
    task_set_current(child);
    exit_impl(exit_status);
    task_set_current(saved_current);
    if (!atomic_read(&child->exited)) {
        errno = ENODATA;
        return -1;
    }
    cursor = parent->children;
    while (cursor && cursor != child) {
        cursor = cursor->next_sibling;
    }
    if (!cursor) {
        errno = ENXIO;
        return -1;
    }
    if (!signal_is_pending(parent, SIGCHLD)) {
        errno = ENOMSG;
        return -1;
    }
    return 0;
}

int wait_job_control_contract_no_children_returns_echild(void) {
    errno = 0;
    if (waitpid_impl(-1, NULL, WNOHANG) != -1 || errno != ECHILD) {
        errno = EPROTO;
        return -1;
    }
    return 0;
}

int wait_job_control_contract_specific_non_child_returns_echild(void) {
    struct task *parent = task_current();
    struct task *child = NULL;
    struct task *grandchild = NULL;
    int result = -1;

    if (!parent) {
        errno = ESRCH;
        return -1;
    }

    child = create_child_task(parent, 0);
    if (!child) {
        return -1;
    }
    grandchild = create_child_task(child, 0);
    if (!grandchild) {
        goto out;
    }

    errno = 0;
    if (waitpid_impl(grandchild->pid, NULL, WNOHANG) != -1 || errno != ECHILD) {
        errno = EPROTO;
        goto out;
    }

    result = 0;

out:
    destroy_child_task(child, grandchild);
    destroy_child_task(parent, child);
    return result;
}

int wait_job_control_contract_wnohang_returns_zero_for_running_child(void) {
    struct task *parent = task_current();
    struct task *child = NULL;
    int result = -1;

    child = create_child_task(parent, 0);
    if (!child) {
        return -1;
    }

    errno = 0;
    if (waitpid_impl(child->pid, NULL, WNOHANG) != 0) {
        errno = EPROTO;
        goto out;
    }

    result = 0;

out:
    destroy_child_task(parent, child);
    return result;
}

int wait_job_control_contract_reaps_exited_child(void) {
    struct task *parent = task_current();
    struct task *child = NULL;
    struct task *lookup = NULL;
    int status = 0;
    int32_t child_pid;

    child = create_child_task(parent, 0);
    if (!child) {
        return -1;
    }
    child_pid = child->pid;
    if (exit_child_with_status(parent, child, 23) != 0) {
        destroy_child_task(parent, child);
        return -1;
    }
    if (waitpid_impl(child_pid, &status, 0) != child_pid) {
        errno = EBUSY;
        return -1;
    }
    if (!wait_status_exited(status) || wait_status_exit_code(status) != 23) {
        errno = ERANGE;
        return -1;
    }
    lookup = task_lookup(child_pid);
    if (lookup) {
        task_put(lookup);
        errno = EPROTO;
        return -1;
    }
    return 0;
}

int wait_job_control_contract_second_wait_after_reap_returns_echild(void) {
    struct task *parent = task_current();
    struct task *child = NULL;
    int32_t child_pid;

    child = create_child_task(parent, 0);
    if (!child) {
        return -1;
    }
    child_pid = child->pid;
    if (exit_child_with_status(parent, child, 7) != 0) {
        destroy_child_task(parent, child);
        return -1;
    }
    if (waitpid_impl(child_pid, NULL, 0) != child_pid) {
        errno = EBUSY;
        return -1;
    }
    errno = 0;
    if (waitpid_impl(child_pid, NULL, WNOHANG) != -1 || errno != ECHILD) {
        errno = EPROTO;
        return -1;
    }
    return 0;
}

int wait_job_control_contract_null_status_still_reaps_exited_child(void) {
    struct task *parent = task_current();
    struct task *child = NULL;
    int32_t child_pid;

    child = create_child_task(parent, 0);
    if (!child) {
        return -1;
    }
    child_pid = child->pid;
    if (exit_child_with_status(parent, child, 11) != 0) {
        destroy_child_task(parent, child);
        return -1;
    }
    if (waitpid_impl(child_pid, NULL, 0) != child_pid) {
        errno = EBUSY;
        return -1;
    }
    errno = 0;
    if (waitpid_impl(child_pid, NULL, WNOHANG) != -1 || errno != ECHILD) {
        errno = EPROTO;
        return -1;
    }
    return 0;
}

int wait_job_control_contract_reports_stopped_child_with_wuntraced(void) {
    struct task *parent = task_current();
    struct task *child = NULL;
    int result;

    child = create_child_task(parent, 0);
    if (!child) {
        return -1;
    }
    result = stop_and_wait_status(parent, child, SIGTSTP, NULL);
    destroy_child_task(parent, child);
    return result;
}

int wait_job_control_contract_stopped_child_without_wuntraced_wnohang_returns_zero(void) {
    struct task *parent = task_current();
    struct task *child = NULL;
    int result = -1;

    child = create_child_task(parent, 0);
    if (!child) {
        return -1;
    }
    clear_pending_signal(parent, SIGCHLD);
    if (signal_generate_task(child, SIGSTOP) != 0) {
        errno = EPROTO;
        goto out;
    }
    if (waitpid_impl(child->pid, NULL, WNOHANG) != 0) {
        errno = EPROTO;
        goto out;
    }
    result = 0;

out:
    destroy_child_task(parent, child);
    return result;
}

int wait_job_control_contract_stopped_child_is_not_reaped(void) {
    struct task *parent = task_current();
    struct task *child = NULL;
    struct task *lookup = NULL;

    child = create_child_task(parent, 0);
    if (!child) {
        return -1;
    }
    if (stop_and_wait_status(parent, child, SIGTSTP, NULL) != 0) {
        destroy_child_task(parent, child);
        return -1;
    }
    lookup = task_lookup(child->pid);
    if (!lookup) {
        errno = EPROTO;
        return -1;
    }
    task_put(lookup);
    destroy_child_task(parent, child);
    return 0;
}

int wait_job_control_contract_reports_continued_child_with_wcontinued(void) {
    struct task *parent = task_current();
    struct task *child = NULL;
    int status = 0;

    child = create_child_task(parent, 0);
    if (!child) {
        return -1;
    }
    if (stop_and_wait_status(parent, child, SIGSTOP, NULL) != 0) {
        destroy_child_task(parent, child);
        return -1;
    }
    clear_pending_signal(parent, SIGCHLD);
    if (signal_generate_task(child, SIGCONT) != 0) {
        destroy_child_task(parent, child);
        errno = EPROTO;
        return -1;
    }
    if (waitpid_impl(child->pid, &status, WCONTINUED) != child->pid) {
        destroy_child_task(parent, child);
        errno = EBUSY;
        return -1;
    }
    if (!wait_status_continued(status)) {
        destroy_child_task(parent, child);
        errno = ERANGE;
        return -1;
    }
    destroy_child_task(parent, child);
    return 0;
}

int wait_job_control_contract_continued_status_is_linux_wifcontinued(void) {
    return wait_job_control_contract_reports_continued_child_with_wcontinued();
}

int wait_job_control_contract_continued_report_is_consumed(void) {
    struct task *parent = task_current();
    struct task *child = NULL;
    int status = 0;
    int result = -1;

    child = create_child_task(parent, 0);
    if (!child) {
        return -1;
    }
    if (stop_and_wait_status(parent, child, SIGSTOP, NULL) != 0) {
        goto out;
    }
    if (signal_generate_task(child, SIGCONT) != 0) {
        errno = EPROTO;
        goto out;
    }
    if (waitpid_impl(child->pid, &status, WCONTINUED) != child->pid ||
        !wait_status_continued(status)) {
        errno = EBUSY;
        goto out;
    }
    if (waitpid_impl(child->pid, NULL, WCONTINUED | WNOHANG) != 0) {
        errno = EPROTO;
        goto out;
    }
    result = 0;

out:
    destroy_child_task(parent, child);
    return result;
}

int wait_job_control_contract_pid_zero_selects_same_process_group(void) {
    struct task *parent = task_current();
    struct task *same_group = NULL;
    struct task *other_group = NULL;
    int status = 0;
    int result = -1;
    int32_t same_group_pid = 0;

    same_group = create_child_task(parent, 0);
    other_group = create_child_task(parent, 1);
    if (!same_group || !other_group) {
        goto out;
    }
    same_group_pid = same_group->pid;
    if (exit_child_with_status(parent, same_group, 3) != 0) {
        goto out;
    }
    if (waitpid_impl(0, &status, 0) != same_group_pid) {
        errno = EBUSY;
        goto out;
    }
    if (!wait_status_exited(status) || wait_status_exit_code(status) != 3) {
        errno = ERANGE;
        goto out;
    }
    result = 0;

out:
    destroy_child_task(parent, other_group);
    return result;
}

int wait_job_control_contract_negative_pid_selects_process_group(void) {
    struct task *parent = task_current();
    struct task *child = NULL;
    int status = 0;
    int32_t group;
    int32_t child_pid;

    child = create_child_task(parent, 1);
    if (!child) {
        return -1;
    }
    group = child->pgid;
    child_pid = child->pid;
    if (exit_child_with_status(parent, child, 9) != 0) {
        destroy_child_task(parent, child);
        return -1;
    }
    if (waitpid_impl(-group, &status, 0) != child_pid) {
        errno = EBUSY;
        return -1;
    }
    if (!wait_status_exited(status) || wait_status_exit_code(status) != 9) {
        errno = ERANGE;
        return -1;
    }
    return 0;
}

int wait_job_control_contract_child_stop_generates_sigchld_for_parent(void) {
    struct task *parent = task_current();
    struct task *child = create_child_task(parent, 0);
    int result;

    if (!child) {
        return -1;
    }
    result = stop_and_wait_status(parent, child, SIGTSTP, NULL);
    destroy_child_task(parent, child);
    return result;
}

int wait_job_control_contract_child_continue_generates_sigchld_for_parent(void) {
    struct task *parent = task_current();
    struct task *child = NULL;
    int status = 0;

    child = create_child_task(parent, 0);
    if (!child) {
        return -1;
    }
    if (stop_and_wait_status(parent, child, SIGSTOP, NULL) != 0) {
        destroy_child_task(parent, child);
        return -1;
    }
    clear_pending_signal(parent, SIGCHLD);
    if (signal_generate_task(child, SIGCONT) != 0) {
        destroy_child_task(parent, child);
        errno = ESRCH;
        return -1;
    }
    if (!signal_is_pending(parent, SIGCHLD)) {
        destroy_child_task(parent, child);
        errno = ENOMSG;
        return -1;
    }
    if (waitpid_impl(child->pid, &status, WCONTINUED) != child->pid ||
        !wait_status_continued(status)) {
        destroy_child_task(parent, child);
        errno = EBUSY;
        return -1;
    }
    destroy_child_task(parent, child);
    return 0;
}

int wait_job_control_contract_child_exit_generates_sigchld_for_parent(void) {
    struct task *parent = task_current();
    struct task *child = NULL;
    int32_t child_pid;

    child = create_child_task(parent, 0);
    if (!child) {
        return -1;
    }
    child_pid = child->pid;
    if (exit_child_with_status(parent, child, 17) != 0) {
        destroy_child_task(parent, child);
        return -1;
    }
    if (waitpid_impl(child_pid, NULL, 0) != child_pid) {
        errno = EBUSY;
        return -1;
    }
    return 0;
}

int wait_job_control_contract_public_waitpid_reports_exited_child_status(void) {
    struct task *parent = task_current();
    struct task *child = NULL;
    int32_t child_pid;
    int status = 0;

    if (!parent) {
        errno = ESRCH;
        return -1;
    }

    child = create_child_task(parent, 0);
    if (!child) {
        return -1;
    }
    child_pid = child->pid;
    if (exit_child_with_status(parent, child, 29) != 0) {
        destroy_child_task(parent, child);
        return -1;
    }

    if (kernel_waitpid(child_pid, &status, 0) != child_pid) {
        errno = EBUSY;
        return -1;
    }
    if (!wait_status_exited(status) || wait_status_exit_code(status) != 29) {
        errno = EPROTO;
        return -1;
    }
    return 0;
}

int wait_job_control_contract_wait4_reports_exited_child_status(void) {
    struct task *parent = task_current();
    struct task *child = NULL;
    int32_t child_pid;
    int status = 0;

    if (!parent) {
        errno = ESRCH;
        return -1;
    }

    child = create_child_task(parent, 0);
    if (!child) {
        return -1;
    }
    child_pid = child->pid;
    if (exit_child_with_status(parent, child, 23) != 0) {
        destroy_child_task(parent, child);
        return -1;
    }

    if (kernel_wait4(child_pid, &status, 0, NULL) != child_pid) {
        errno = EBUSY;
        return -1;
    }
    if (!wait_status_exited(status) || wait_status_exit_code(status) != 23) {
        errno = EPROTO;
        return -1;
    }
    return 0;
}

int wait_job_control_contract_public_waitpid_reports_stopped_child_with_wuntraced(void) {
    struct task *parent = task_current();
    struct task *child = NULL;
    int status = 0;

    if (!parent) {
        errno = ESRCH;
        return -1;
    }

    child = create_child_task(parent, 0);
    if (!child) {
        return -1;
    }
    clear_pending_signal(parent, SIGCHLD);
    if (signal_generate_task(child, SIGSTOP) != 0) {
        destroy_child_task(parent, child);
        return -1;
    }

    if (kernel_waitpid(child->pid, &status, WUNTRACED) != child->pid) {
        destroy_child_task(parent, child);
        errno = EBUSY;
        return -1;
    }
    if (!wait_status_stopped(status) || wait_status_stop_signal(status) != SIGSTOP) {
        destroy_child_task(parent, child);
        errno = EPROTO;
        return -1;
    }

    destroy_child_task(parent, child);
    return 0;
}

int wait_job_control_contract_public_waitpid_reports_continued_child_with_wcontinued(void) {
    struct task *parent = task_current();
    struct task *child = NULL;
    int status = 0;

    if (!parent) {
        errno = ESRCH;
        return -1;
    }

    child = create_child_task(parent, 0);
    if (!child) {
        return -1;
    }
    if (stop_and_wait_status(parent, child, SIGSTOP, NULL) != 0) {
        destroy_child_task(parent, child);
        return -1;
    }
    clear_pending_signal(parent, SIGCHLD);
    if (signal_generate_task(child, SIGCONT) != 0) {
        destroy_child_task(parent, child);
        return -1;
    }

    if (kernel_waitpid(child->pid, &status, WCONTINUED) != child->pid) {
        destroy_child_task(parent, child);
        errno = EBUSY;
        return -1;
    }
    if (!wait_status_continued(status)) {
        destroy_child_task(parent, child);
        errno = EPROTO;
        return -1;
    }

    destroy_child_task(parent, child);
    return 0;
}

int wait_job_control_contract_waitid_reports_exited_child_status(void) {
    struct task *parent = task_current();
    struct task *child = NULL;
    int32_t child_pid;
    siginfo_t info;

    if (!parent) {
        errno = ESRCH;
        return -1;
    }

    child = create_child_task(parent, 0);
    if (!child) {
        return -1;
    }
    child_pid = child->pid;
    if (exit_child_with_status(parent, child, 31) != 0) {
        destroy_child_task(parent, child);
        return -1;
    }

    memset(&info, 0, sizeof(info));
    if (kernel_waitid(P_PID, child_pid, &info, WEXITED) != 0) {
        return -1;
    }
    if (info.si_signo != SIGCHLD || info.si_pid != child_pid || info.si_code != CLD_EXITED ||
        info.si_status != 31) {
        errno = EPROTO;
        return -1;
    }
    return 0;
}

int wait_job_control_contract_waitid_wnowait_preserves_waitable_child(void) {
    struct task *parent = task_current();
    struct task *child = NULL;
    int32_t child_pid;
    siginfo_t info;
    int status = 0;

    if (!parent) {
        errno = ESRCH;
        return -1;
    }

    child = create_child_task(parent, 0);
    if (!child) {
        return -1;
    }
    child_pid = child->pid;
    if (exit_child_with_status(parent, child, 9) != 0) {
        destroy_child_task(parent, child);
        return -1;
    }

    memset(&info, 0, sizeof(info));
    if (kernel_waitid(P_PID, child_pid, &info, WEXITED | WNOWAIT) != 0) {
        return -1;
    }
    if (info.si_signo != SIGCHLD || info.si_pid != child_pid || info.si_code != CLD_EXITED ||
        info.si_status != 9) {
        errno = EPROTO;
        return -1;
    }

    if (waitpid_impl(child_pid, &status, 0) != child_pid) {
        errno = EBUSY;
        return -1;
    }
    if (!wait_status_exited(status) || wait_status_exit_code(status) != 9) {
        errno = EPROTO;
        return -1;
    }
    return 0;
}

static int expect_child_io_stop(long io_result, int expected_errno) {
    if (io_result >= 0 || errno != expected_errno) {
        errno = EPROTO;
        return -1;
    }
    return 0;
}

static int expect_parent_wait_stop(struct task *parent, struct task *child, int expected_signal) {
    int status = 0;

    if (waitpid_impl(child->pid, &status, WUNTRACED) != child->pid) {
        errno = EBUSY;
        return -1;
    }
    if (!wait_status_stopped(status) || wait_status_stop_signal(status) != expected_signal) {
        errno = ERANGE;
        return -1;
    }
    if (!signal_is_pending(parent, SIGCHLD)) {
        errno = ENOMSG;
        return -1;
    }
    return 0;
}

int wait_job_control_contract_pty_background_read_stop_is_waitpid_visible(void) {
    struct task *parent = task_current();
    struct task *child = NULL;
    struct task *saved_current = parent;
    int master_fd = -1;
    int slave_fd = -1;
    unsigned int pty_index = 0;
    char byte = '\0';
    int result = -1;

    if (detach_controlling_tty_if_present() != 0) {
        return -1;
    }
    if (alloc_pty_pair(&master_fd, &slave_fd, &pty_index) != 0) {
        return -1;
    }
    if (pty_contract_ioctl(slave_fd, TIOCSCTTY, 0) != 0) {
        goto out;
    }
    child = create_child_task(parent, 1);
    if (!child) {
        goto out;
    }

    clear_pending_signal(parent, SIGCHLD);
    task_set_current(child);
    errno = 0;
    if (expect_child_io_stop(read_impl(slave_fd, &byte, 1), EINTR) != 0) {
        task_set_current(saved_current);
        goto out;
    }
    task_set_current(saved_current);
    if (expect_parent_wait_stop(parent, child, SIGTTIN) != 0) {
        goto out;
    }
    result = 0;

out:
    task_set_current(saved_current);
    destroy_child_task(parent, child);
    close_if_open(master_fd);
    close_if_open(slave_fd);
    detach_controlling_tty_if_present();
    return result;
}

int wait_job_control_contract_pty_background_write_tostop_stop_is_waitpid_visible(void) {
    struct task *parent = task_current();
    struct task *child = NULL;
    struct task *saved_current = parent;
    int master_fd = -1;
    int slave_fd = -1;
    unsigned int pty_index = 0;
    pty_linux_termios_t termios;
    char byte = 'x';
    int result = -1;

    if (detach_controlling_tty_if_present() != 0) {
        return -1;
    }
    if (alloc_pty_pair(&master_fd, &slave_fd, &pty_index) != 0) {
        return -1;
    }
    if (pty_contract_ioctl(slave_fd, TIOCSCTTY, 0) != 0) {
        goto out;
    }
    if (pty_get_termios_impl(pty_index, &termios) != 0) {
        goto out;
    }
    termios.c_lflag |= PTY_LFLAG_TOSTOP;
    if (pty_set_termios_impl(pty_index, &termios) != 0) {
        goto out;
    }
    child = create_child_task(parent, 1);
    if (!child) {
        goto out;
    }

    clear_pending_signal(parent, SIGCHLD);
    task_set_current(child);
    errno = 0;
    if (expect_child_io_stop(write_impl(slave_fd, &byte, 1), EINTR) != 0) {
        task_set_current(saved_current);
        goto out;
    }
    task_set_current(saved_current);
    if (expect_parent_wait_stop(parent, child, SIGTTOU) != 0) {
        goto out;
    }
    result = 0;

out:
    task_set_current(saved_current);
    destroy_child_task(parent, child);
    close_if_open(master_fd);
    close_if_open(slave_fd);
    detach_controlling_tty_if_present();
    return result;
}

int wait_job_control_contract_pty_vsusp_stop_is_waitpid_visible(void) {
    struct task *parent = task_current();
    struct task *child = NULL;
    int master_fd = -1;
    int slave_fd = -1;
    unsigned int pty_index = 0;
    unsigned char vsusp = 26;
    int status = 0;
    int result = -1;

    if (detach_controlling_tty_if_present() != 0) {
        return -1;
    }
    if (alloc_pty_pair(&master_fd, &slave_fd, &pty_index) != 0) {
        return -1;
    }
    if (pty_contract_ioctl(slave_fd, TIOCSCTTY, 0) != 0) {
        goto out;
    }
    child = create_child_task(parent, 1);
    if (!child) {
        goto out;
    }
    if (pty_contract_ioctl(slave_fd, TIOCSPGRP, &child->pgid) != 0) {
        goto out;
    }

    clear_pending_signal(parent, SIGCHLD);
    if (pty_write_master_impl(pty_index, &vsusp, 1, false) != 1) {
        goto out;
    }
    if (waitpid_impl(child->pid, &status, WUNTRACED) != child->pid) {
        errno = EBUSY;
        goto out;
    }
    if (!wait_status_stopped(status) || wait_status_stop_signal(status) != SIGTSTP) {
        errno = ERANGE;
        goto out;
    }
    if (!signal_is_pending(parent, SIGCHLD)) {
        errno = ENOMSG;
        goto out;
    }

    result = 0;

out:
    destroy_child_task(parent, child);
    close_if_open(master_fd);
    close_if_open(slave_fd);
    detach_controlling_tty_if_present();
    return result;
}

int wait_job_control_contract_waitpid_signal_interrupt_records_restart(void) {
    struct task *parent = task_current();
    struct task *waiter = NULL;
    struct task *child = NULL;
    struct task *restore;
    struct waitpid_restart_thread ctx;
    kernel_thread_t thread;
    long ret;
    int status = 0;
    int32_t expected_pid;

    if (!parent) {
        errno = ESRCH;
        return -1;
    }
    waiter = task_create_child_impl(parent);
    if (!waiter) {
        return -1;
    }
    child = task_create_child_impl(waiter);
    if (!child) {
        task_unlink_child_impl(parent, waiter);
        task_put(waiter);
        return -1;
    }
    expected_pid = child->pid;

    waitpid_restart_thread_init(&ctx);
    ctx.task = waiter;
    ctx.child_pid = child->pid;
    ctx.status = 0;
    ctx.result = 0;
    ctx.saved_errno = 0;

    if (kernel_thread_create(&thread, NULL, waitpid_restart_thread_main, &ctx) != 0) {
        waitpid_restart_thread_destroy(&ctx);
        destroy_child_task(waiter, child);
        task_unlink_child_impl(parent, waiter);
        task_put(waiter);
        errno = ECHILD;
        return -1;
    }
    kernel_thread_detach(thread);
    waitpid_restart_thread_wait_started(&ctx);
    if (signal_generate_task(waiter, SIGUSR1) != 0) {
        waitpid_restart_thread_wait_done(&ctx);
        waitpid_restart_thread_destroy(&ctx);
        destroy_child_task(waiter, child);
        task_unlink_child_impl(parent, waiter);
        task_put(waiter);
        return -1;
    }
    waitpid_restart_thread_wait_done(&ctx);
    waitpid_restart_thread_destroy(&ctx);

    if (ctx.result != -1 || ctx.saved_errno != EINTR ||
        !waiter->mm ||
        waiter->mm->signal_frame_restart_kind != TASK_RESTART_WAITPID ||
        waiter->mm->signal_frame_restart_arg0 != (uint64_t)(int64_t)expected_pid ||
        waiter->mm->signal_frame_restart_arg1 != (uint64_t)(uintptr_t)&ctx.status ||
        waiter->mm->signal_frame_restart_arg2 != 0) {
        destroy_child_task(waiter, child);
        task_unlink_child_impl(parent, waiter);
        task_put(waiter);
        errno = ENODATA;
        return -1;
    }

    clear_pending_signal(waiter, SIGUSR1);
    clear_pending_signal(waiter, SIGCHLD);
    task_mark_exited(child, 7);
    task_notify_parent_state_change(child);

    restore = task_current();
    task_set_current(waiter);
    ret = syscall_dispatch_impl(__NR_restart_syscall, 0, 0, 0, 0, 0, 0);
    status = ctx.status;
    task_set_current(restore);

    task_unlink_child_impl(parent, waiter);
    task_put(waiter);

    if (ret != expected_pid || !wait_status_exited(status) ||
        wait_status_exit_code(status) != 7) {
        errno = EPROTO;
        return -1;
    }
    return 0;
}

int wait_job_control_contract_clone_thread_is_not_waitable(void) {
    struct task *parent = task_current();
    struct task *child = NULL;
    int32_t child_pid;
    int status = 0;
    int result = -1;

    if (!parent) {
        errno = ESRCH;
        return -1;
    }

    child_pid = clone_impl(CLONE_VM | CLONE_THREAD | CLONE_SIGHAND);
    if (child_pid < 0) {
        return -1;
    }
    child = task_lookup(child_pid);
    if (!child) {
        errno = ESRCH;
        return -1;
    }

    errno = 0;
    if (waitpid_impl(child_pid, &status, WNOHANG) != -1 || errno != ECHILD) {
        errno = EPROTO;
        goto out;
    }
    result = 0;

out: {
        int saved_errno = errno;
        task_unlink_child_impl(parent, child);
        task_put(child);
        task_put(child);
        errno = saved_errno;
    }
    return result;
}

static void wait_job_control_suite_init(struct kunit *test) {
    (void)test;
    reset_wait_job_control_test_kernel_state();
}

static void test_no_children_returns_echild(struct kunit *test) {
    if (wait_job_control_contract_no_children_returns_echild() != 0) {
        KUNIT_FAIL(test, "no_children_returns_echild failed with errno %d", errno);
    }
}

static void test_specific_non_child_returns_echild(struct kunit *test) {
    if (wait_job_control_contract_specific_non_child_returns_echild() != 0) {
        KUNIT_FAIL(test, "specific_non_child_returns_echild failed with errno %d", errno);
    }
}

static void test_wnohang_returns_zero_for_running_child(struct kunit *test) {
    if (wait_job_control_contract_wnohang_returns_zero_for_running_child() != 0) {
        KUNIT_FAIL(test, "wnohang_returns_zero_for_running_child failed with errno %d", errno);
    }
}

static void test_reaps_exited_child(struct kunit *test) {
    if (wait_job_control_contract_reaps_exited_child() != 0) {
        KUNIT_FAIL(test, "reaps_exited_child failed with errno %d", errno);
    }
}

static void test_second_wait_after_reap_returns_echild(struct kunit *test) {
    if (wait_job_control_contract_second_wait_after_reap_returns_echild() != 0) {
        KUNIT_FAIL(test, "second_wait_after_reap_returns_echild failed with errno %d", errno);
    }
}

static void test_null_status_still_reaps_exited_child(struct kunit *test) {
    if (wait_job_control_contract_null_status_still_reaps_exited_child() != 0) {
        KUNIT_FAIL(test, "null_status_still_reaps_exited_child failed with errno %d", errno);
    }
}

static void test_reports_stopped_child_with_wuntraced(struct kunit *test) {
    if (wait_job_control_contract_reports_stopped_child_with_wuntraced() != 0) {
        KUNIT_FAIL(test, "reports_stopped_child_with_wuntraced failed with errno %d", errno);
    }
}

static void test_stopped_child_without_wuntraced_wnohang_returns_zero(struct kunit *test) {
    if (wait_job_control_contract_stopped_child_without_wuntraced_wnohang_returns_zero() != 0) {
        KUNIT_FAIL(test, "stopped_child_without_wuntraced_wnohang_returns_zero failed with errno %d", errno);
    }
}

static void test_stopped_child_is_not_reaped(struct kunit *test) {
    if (wait_job_control_contract_stopped_child_is_not_reaped() != 0) {
        KUNIT_FAIL(test, "stopped_child_is_not_reaped failed with errno %d", errno);
    }
}

static void test_reports_continued_child_with_wcontinued(struct kunit *test) {
    if (wait_job_control_contract_reports_continued_child_with_wcontinued() != 0) {
        KUNIT_FAIL(test, "reports_continued_child_with_wcontinued failed with errno %d", errno);
    }
}

static void test_continued_status_is_linux_wifcontinued(struct kunit *test) {
    if (wait_job_control_contract_continued_status_is_linux_wifcontinued() != 0) {
        KUNIT_FAIL(test, "continued_status_is_linux_wifcontinued failed with errno %d", errno);
    }
}

static void test_continued_report_is_consumed(struct kunit *test) {
    if (wait_job_control_contract_continued_report_is_consumed() != 0) {
        KUNIT_FAIL(test, "continued_report_is_consumed failed with errno %d", errno);
    }
}

static void test_pid_zero_selects_same_process_group(struct kunit *test) {
    if (wait_job_control_contract_pid_zero_selects_same_process_group() != 0) {
        KUNIT_FAIL(test, "pid_zero_selects_same_process_group failed with errno %d", errno);
    }
}

static void test_negative_pid_selects_process_group(struct kunit *test) {
    if (wait_job_control_contract_negative_pid_selects_process_group() != 0) {
        KUNIT_FAIL(test, "negative_pid_selects_process_group failed with errno %d", errno);
    }
}

static void test_child_stop_generates_sigchld_for_parent(struct kunit *test) {
    if (wait_job_control_contract_child_stop_generates_sigchld_for_parent() != 0) {
        KUNIT_FAIL(test, "child_stop_generates_sigchld_for_parent failed with errno %d", errno);
    }
}

static void test_child_continue_generates_sigchld_for_parent(struct kunit *test) {
    if (wait_job_control_contract_child_continue_generates_sigchld_for_parent() != 0) {
        KUNIT_FAIL(test, "child_continue_generates_sigchld_for_parent failed with errno %d", errno);
    }
}

static void test_child_exit_generates_sigchld_for_parent(struct kunit *test) {
    if (wait_job_control_contract_child_exit_generates_sigchld_for_parent() != 0) {
        KUNIT_FAIL(test, "child_exit_generates_sigchld_for_parent failed with errno %d", errno);
    }
}

static void test_public_waitpid_reports_exited_child_status(struct kunit *test) {
    if (wait_job_control_contract_public_waitpid_reports_exited_child_status() != 0) {
        KUNIT_FAIL(test, "public_waitpid_reports_exited_child_status failed with errno %d", errno);
    }
}

static void test_wait4_reports_exited_child_status(struct kunit *test) {
    if (wait_job_control_contract_wait4_reports_exited_child_status() != 0) {
        KUNIT_FAIL(test, "wait4_reports_exited_child_status failed with errno %d", errno);
    }
}

static void test_public_waitpid_reports_stopped_child_with_wuntraced(struct kunit *test) {
    if (wait_job_control_contract_public_waitpid_reports_stopped_child_with_wuntraced() != 0) {
        KUNIT_FAIL(test, "public_waitpid_reports_stopped_child_with_wuntraced failed with errno %d", errno);
    }
}

static void test_public_waitpid_reports_continued_child_with_wcontinued(struct kunit *test) {
    if (wait_job_control_contract_public_waitpid_reports_continued_child_with_wcontinued() != 0) {
        KUNIT_FAIL(test, "public_waitpid_reports_continued_child_with_wcontinued failed with errno %d", errno);
    }
}

static void test_waitid_reports_exited_child_status(struct kunit *test) {
    if (wait_job_control_contract_waitid_reports_exited_child_status() != 0) {
        KUNIT_FAIL(test, "waitid_reports_exited_child_status failed with errno %d", errno);
    }
}

static void test_waitid_wnowait_preserves_waitable_child(struct kunit *test) {
    if (wait_job_control_contract_waitid_wnowait_preserves_waitable_child() != 0) {
        KUNIT_FAIL(test, "waitid_wnowait_preserves_waitable_child failed with errno %d", errno);
    }
}

static void test_pty_background_read_stop_is_waitpid_visible(struct kunit *test) {
    if (wait_job_control_contract_pty_background_read_stop_is_waitpid_visible() != 0) {
        KUNIT_FAIL(test, "pty_background_read_stop_is_waitpid_visible failed with errno %d", errno);
    }
}

static void test_pty_background_write_tostop_stop_is_waitpid_visible(struct kunit *test) {
    if (wait_job_control_contract_pty_background_write_tostop_stop_is_waitpid_visible() != 0) {
        KUNIT_FAIL(test, "pty_background_write_tostop_stop_is_waitpid_visible failed with errno %d", errno);
    }
}

static void test_pty_vsusp_stop_is_waitpid_visible(struct kunit *test) {
    if (wait_job_control_contract_pty_vsusp_stop_is_waitpid_visible() != 0) {
        KUNIT_FAIL(test, "pty_vsusp_stop_is_waitpid_visible failed with errno %d", errno);
    }
}

static void test_waitpid_signal_interrupt_records_restart(struct kunit *test) {
    if (wait_job_control_contract_waitpid_signal_interrupt_records_restart() != 0) {
        KUNIT_FAIL(test, "waitpid_signal_interrupt_records_restart failed with errno %d", errno);
    }
}

static void test_clone_thread_is_not_waitable(struct kunit *test) {
    if (wait_job_control_contract_clone_thread_is_not_waitable() != 0) {
        KUNIT_FAIL(test, "clone_thread_is_not_waitable failed with errno %d", errno);
    }
}

static void wait_job_control_suite_exit(struct kunit *test) {
    (void)test;
    reset_wait_job_control_test_kernel_state();
}

static const struct kunit_case wait_job_control_cases[] = {
    KUNIT_CASE(test_no_children_returns_echild),
    KUNIT_CASE(test_specific_non_child_returns_echild),
    KUNIT_CASE(test_wnohang_returns_zero_for_running_child),
    KUNIT_CASE(test_reaps_exited_child),
    KUNIT_CASE(test_second_wait_after_reap_returns_echild),
    KUNIT_CASE(test_null_status_still_reaps_exited_child),
    KUNIT_CASE(test_reports_stopped_child_with_wuntraced),
    KUNIT_CASE(test_stopped_child_without_wuntraced_wnohang_returns_zero),
    KUNIT_CASE(test_stopped_child_is_not_reaped),
    KUNIT_CASE(test_reports_continued_child_with_wcontinued),
    KUNIT_CASE(test_continued_status_is_linux_wifcontinued),
    KUNIT_CASE(test_continued_report_is_consumed),
    KUNIT_CASE(test_pid_zero_selects_same_process_group),
    KUNIT_CASE(test_negative_pid_selects_process_group),
    KUNIT_CASE(test_child_stop_generates_sigchld_for_parent),
    KUNIT_CASE(test_child_continue_generates_sigchld_for_parent),
    KUNIT_CASE(test_child_exit_generates_sigchld_for_parent),
    KUNIT_CASE(test_public_waitpid_reports_exited_child_status),
    KUNIT_CASE(test_wait4_reports_exited_child_status),
    KUNIT_CASE(test_public_waitpid_reports_stopped_child_with_wuntraced),
    KUNIT_CASE(test_public_waitpid_reports_continued_child_with_wcontinued),
    KUNIT_CASE(test_waitid_reports_exited_child_status),
    KUNIT_CASE(test_waitid_wnowait_preserves_waitable_child),
    KUNIT_CASE(test_pty_background_read_stop_is_waitpid_visible),
    KUNIT_CASE(test_pty_background_write_tostop_stop_is_waitpid_visible),
    KUNIT_CASE(test_pty_vsusp_stop_is_waitpid_visible),
    KUNIT_CASE(test_waitpid_signal_interrupt_records_restart),
    KUNIT_CASE(test_clone_thread_is_not_waitable),
};

static const struct kunit_suite wait_job_control_suite = {
    .name = "wait_job_control",
    .cases = wait_job_control_cases,
    .case_count = sizeof(wait_job_control_cases) / sizeof(wait_job_control_cases[0]),
    .init = wait_job_control_suite_init,
    .exit = wait_job_control_suite_exit,
};

const struct kunit_suite *kernel_wait_suite(void) {
    return &wait_job_control_suite;
}

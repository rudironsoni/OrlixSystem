#include <asm/ioctls.h>
#include <asm/unistd.h>
#include <linux/fcntl.h>
#include <linux/wait.h>
#define __ASSEMBLY__ 1
#include <asm-generic/signal.h>
#undef __ASSEMBLY__

#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "fs/fdtable.h"
#include "fs/pty.h"
#include "runtime/syscall.h"
#include "kernel/signal.h"
#include "kernel/task.h"
#include "kernel/wait.h"

extern int open_impl(const char *pathname, int flags, linux_mode_t mode);
extern int close_impl(int fd);
extern long read_impl(int fd, void *buf, size_t count);
extern long write_impl(int fd, const void *buf, size_t count);
extern int pty_contract_ioctl(int fd, unsigned long request, ...);

#ifndef WIFEXITED
#define WIFEXITED(status) (((status) & 0x7f) == 0)
#endif
#ifndef WEXITSTATUS
#define WEXITSTATUS(status) (((status) >> 8) & 0xff)
#endif
#ifndef WIFSIGNALED
#define WIFSIGNALED(status) (((status) & 0x7f) != 0 && ((status) & 0x7f) != 0x7f)
#endif
#ifndef WTERMSIG
#define WTERMSIG(status) ((status) & 0x7f)
#endif
#ifndef WIFSTOPPED
#define WIFSTOPPED(status) (((status) & 0xff) == 0x7f)
#endif
#ifndef WSTOPSIG
#define WSTOPSIG(status) (((status) >> 8) & 0xff)
#endif
#ifndef WIFCONTINUED
#define WIFCONTINUED(status) ((status) == 0xffff)
#endif

static int close_if_open(int fd) {
    if (fd >= 0) {
        return close_impl(fd);
    }
    return 0;
}

static void clear_pending_signal(struct task_struct *task, int32_t sig) {
    if (!task || !task->signal || sig < 1 || sig > KERNEL_SIG_NUM) {
        return;
    }
    task->thread_pending_signals &= ~(1ULL << ((sig - 1) & 63));
    task->signal->shared_pending.sig[(sig - 1) >> 6] &= ~(1ULL << ((sig - 1) & 63));
}

struct waitpid_restart_thread {
    struct task_struct *task;
    int32_t child_pid;
    int status;
    int32_t result;
    int saved_errno;
    atomic_int ready;
};

static void *waitpid_restart_thread_main(void *arg) {
    struct waitpid_restart_thread *ctx = arg;

    set_current(ctx->task);
    atomic_store(&ctx->ready, 1);
    ctx->result = waitpid_impl(ctx->child_pid, &ctx->status, 0);
    ctx->saved_errno = errno;
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

static struct task_struct *create_child_task(struct task_struct *parent, int own_pgrp) {
    struct task_struct *child;

    child = task_create_child_impl(parent);
    if (!child) {
        return NULL;
    }

    if (own_pgrp && setpgid_impl(child->pid, child->pid) != 0) {
        task_unlink_child_impl(parent, child);
        free_task(child);
        return NULL;
    }

    return child;
}

static void destroy_child_task(struct task_struct *parent, struct task_struct *child) {
    if (!child) {
        return;
    }
    if (parent) {
        task_unlink_child_impl(parent, child);
    }
    free_task(child);
}

static int stop_and_wait_status(struct task_struct *parent, struct task_struct *child, int32_t sig, int *status_out) {
    struct task_struct *cursor;
    int status = 0;
    int32_t waited;

    clear_pending_signal(parent, SIGCHLD);
    if (signal_generate_task(child, sig) != 0) {
        errno = ESRCH;
        return -1;
    }
    if (!atomic_load(&child->stop_report_pending) || !atomic_load(&child->stopped)) {
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
    if (!WIFSTOPPED(status) || WSTOPSIG(status) != sig) {
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

static int exit_child_with_status(struct task_struct *parent, struct task_struct *child, int exit_status) {
    struct task_struct *saved_current = get_current();
    struct task_struct *cursor;
    clear_pending_signal(parent, SIGCHLD);
    set_current(child);
    exit_impl(exit_status);
    set_current(saved_current);
    if (!atomic_load(&child->exited)) {
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
    struct task_struct *parent = get_current();
    struct task_struct *child = NULL;
    struct task_struct *grandchild = NULL;
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
    struct task_struct *parent = get_current();
    struct task_struct *child = NULL;
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
    struct task_struct *parent = get_current();
    struct task_struct *child = NULL;
    struct task_struct *lookup = NULL;
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
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 23) {
        errno = ERANGE;
        return -1;
    }
    lookup = task_lookup(child_pid);
    if (lookup) {
        free_task(lookup);
        errno = EPROTO;
        return -1;
    }
    return 0;
}

int wait_job_control_contract_second_wait_after_reap_returns_echild(void) {
    struct task_struct *parent = get_current();
    struct task_struct *child = NULL;
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
    struct task_struct *parent = get_current();
    struct task_struct *child = NULL;
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
    struct task_struct *parent = get_current();
    struct task_struct *child = NULL;
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
    struct task_struct *parent = get_current();
    struct task_struct *child = NULL;
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
    struct task_struct *parent = get_current();
    struct task_struct *child = NULL;
    struct task_struct *lookup = NULL;

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
    free_task(lookup);
    destroy_child_task(parent, child);
    return 0;
}

int wait_job_control_contract_reports_continued_child_with_wcontinued(void) {
    struct task_struct *parent = get_current();
    struct task_struct *child = NULL;
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
    if (!WIFCONTINUED(status)) {
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
    struct task_struct *parent = get_current();
    struct task_struct *child = NULL;
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
    if (waitpid_impl(child->pid, &status, WCONTINUED) != child->pid || !WIFCONTINUED(status)) {
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
    struct task_struct *parent = get_current();
    struct task_struct *same_group = NULL;
    struct task_struct *other_group = NULL;
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
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 3) {
        errno = ERANGE;
        goto out;
    }
    result = 0;

out:
    destroy_child_task(parent, other_group);
    return result;
}

int wait_job_control_contract_negative_pid_selects_process_group(void) {
    struct task_struct *parent = get_current();
    struct task_struct *child = NULL;
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
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 9) {
        errno = ERANGE;
        return -1;
    }
    return 0;
}

int wait_job_control_contract_child_stop_generates_sigchld_for_parent(void) {
    struct task_struct *parent = get_current();
    struct task_struct *child = create_child_task(parent, 0);
    int result;

    if (!child) {
        return -1;
    }
    result = stop_and_wait_status(parent, child, SIGTSTP, NULL);
    destroy_child_task(parent, child);
    return result;
}

int wait_job_control_contract_child_continue_generates_sigchld_for_parent(void) {
    struct task_struct *parent = get_current();
    struct task_struct *child = NULL;
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
    if (waitpid_impl(child->pid, &status, WCONTINUED) != child->pid || !WIFCONTINUED(status)) {
        destroy_child_task(parent, child);
        errno = EBUSY;
        return -1;
    }
    destroy_child_task(parent, child);
    return 0;
}

int wait_job_control_contract_child_exit_generates_sigchld_for_parent(void) {
    struct task_struct *parent = get_current();
    struct task_struct *child = NULL;
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

static int expect_child_io_stop(long io_result, int expected_errno) {
    if (io_result >= 0 || errno != expected_errno) {
        errno = EPROTO;
        return -1;
    }
    return 0;
}

static int expect_parent_wait_stop(struct task_struct *parent, struct task_struct *child, int expected_signal) {
    int status = 0;

    if (waitpid_impl(child->pid, &status, WUNTRACED) != child->pid) {
        errno = EBUSY;
        return -1;
    }
    if (!WIFSTOPPED(status) || WSTOPSIG(status) != expected_signal) {
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
    struct task_struct *parent = get_current();
    struct task_struct *child = NULL;
    struct task_struct *saved_current = parent;
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
    set_current(child);
    errno = 0;
    if (expect_child_io_stop(read_impl(slave_fd, &byte, 1), EINTR) != 0) {
        set_current(saved_current);
        goto out;
    }
    set_current(saved_current);
    if (expect_parent_wait_stop(parent, child, SIGTTIN) != 0) {
        goto out;
    }
    result = 0;

out:
    set_current(saved_current);
    destroy_child_task(parent, child);
    close_if_open(master_fd);
    close_if_open(slave_fd);
    detach_controlling_tty_if_present();
    return result;
}

int wait_job_control_contract_pty_background_write_tostop_stop_is_waitpid_visible(void) {
    struct task_struct *parent = get_current();
    struct task_struct *child = NULL;
    struct task_struct *saved_current = parent;
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
    set_current(child);
    errno = 0;
    if (expect_child_io_stop(write_impl(slave_fd, &byte, 1), EINTR) != 0) {
        set_current(saved_current);
        goto out;
    }
    set_current(saved_current);
    if (expect_parent_wait_stop(parent, child, SIGTTOU) != 0) {
        goto out;
    }
    result = 0;

out:
    set_current(saved_current);
    destroy_child_task(parent, child);
    close_if_open(master_fd);
    close_if_open(slave_fd);
    detach_controlling_tty_if_present();
    return result;
}

int wait_job_control_contract_pty_vsusp_stop_is_waitpid_visible(void) {
    struct task_struct *parent = get_current();
    struct task_struct *child = NULL;
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
    if (!WIFSTOPPED(status) || WSTOPSIG(status) != SIGTSTP) {
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
    struct task_struct *parent = get_current();
    struct task_struct *waiter = NULL;
    struct task_struct *child = NULL;
    struct task_struct *restore;
    struct waitpid_restart_thread ctx;
    pthread_t thread;
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
        free_task(waiter);
        return -1;
    }
    expected_pid = child->pid;

    ctx.task = waiter;
    ctx.child_pid = child->pid;
    ctx.status = 0;
    ctx.result = 0;
    ctx.saved_errno = 0;
    atomic_init(&ctx.ready, 0);

    if (pthread_create(&thread, NULL, waitpid_restart_thread_main, &ctx) != 0) {
        destroy_child_task(waiter, child);
        task_unlink_child_impl(parent, waiter);
        free_task(waiter);
        errno = ECHILD;
        return -1;
    }
    while (atomic_load(&ctx.ready) == 0) {
        /* spin until the waiter has entered waitpid_impl */
    }
    if (signal_generate_task(waiter, SIGUSR1) != 0) {
        pthread_join(thread, NULL);
        destroy_child_task(waiter, child);
        task_unlink_child_impl(parent, waiter);
        free_task(waiter);
        return -1;
    }
    pthread_join(thread, NULL);

    if (ctx.result != -1 || ctx.saved_errno != EINTR ||
        !waiter->mm ||
        waiter->mm->signal_frame_restart_kind != TASK_RESTART_WAITPID ||
        waiter->mm->signal_frame_restart_arg0 != (uint64_t)(int64_t)expected_pid ||
        waiter->mm->signal_frame_restart_arg1 != (uint64_t)(uintptr_t)&ctx.status ||
        waiter->mm->signal_frame_restart_arg2 != 0) {
        destroy_child_task(waiter, child);
        task_unlink_child_impl(parent, waiter);
        free_task(waiter);
        errno = ENODATA;
        return -1;
    }

    clear_pending_signal(waiter, SIGUSR1);
    clear_pending_signal(waiter, SIGCHLD);
    task_mark_exited(child, 7);
    task_notify_parent_state_change(child);

    restore = get_current();
    set_current(waiter);
    ret = syscall_dispatch_impl(__NR_restart_syscall, 0, 0, 0, 0, 0, 0);
    status = ctx.status;
    set_current(restore);

    task_unlink_child_impl(parent, waiter);
    free_task(waiter);

    if (ret != expected_pid || !WIFEXITED(status) || WEXITSTATUS(status) != 7) {
        errno = EPROTO;
        return -1;
    }
    return 0;
}

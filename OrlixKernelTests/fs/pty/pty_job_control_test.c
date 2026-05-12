#include <asm/ioctls.h>
#include <uapi/linux/errno.h>
#include <uapi/linux/fcntl.h>
#include <uapi/linux/signal.h>
#include <linux/string.h>

#include "../../kunit/kunit.h"
#include "../../kunit/suite_registry.h"
#include "kernel/init.h"
#include "fs/fdtable.h"
#include "fs/pty.h"
#include "kernel/signal.h"
#include "kernel/task.h"

extern int open_impl(const char *pathname, int flags, uint32_t mode);
extern int close_impl(int fd);
extern long read_impl(int fd, void *buf, size_t count);
extern long write_impl(int fd, const void *buf, size_t count);
extern int pty_contract_ioctl(int fd, unsigned long request, ...);
extern __kernel_pid_t tcgetpgrp(int fd);
extern int tcsetpgrp(int fd, __kernel_pid_t pgrp);
extern int killpg(int pgrp, int sig);
extern int errno;

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

static struct task *alloc_session_peer(int32_t sid, int32_t pgid, int32_t ppid) {
    struct task *task = alloc_task();
    if (!task) {
        errno = ENOMEM;
        return NULL;
    }

    task->ppid = ppid;
    task->sid = sid;
    task->pgid = pgid;
    task->signal = alloc_signal_struct();
    if (!task->signal) {
        task_put(task);
        errno = ENOMEM;
        return NULL;
    }

    return task;
}

static void clear_pending_signal(struct task *task, int32_t sig) {
    if (!task || !task->signal || sig < 1 || sig > KERNEL_SIG_NUM) {
        return;
    }

    task->thread_pending_signals &= ~(1ULL << ((sig - 1) & 63));
    task->signal->shared_pending.sig[(sig - 1) >> 6] &= ~(1ULL << ((sig - 1) & 63));
}

static void reset_pty_job_control_test_kernel_state(void) {
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
        task_put(child);
    }
}

int pty_job_control_contract_tiocspgrp_round_trip(void) {
    struct task *task = task_current();
    struct task *peer = NULL;
    int master_fd = -1;
    int slave_fd = -1;
    unsigned int pty_index = 0;
    int32_t foreground_pgrp = 0;
    int result = -1;

    if (!task) {
        errno = ESRCH;
        return -1;
    }

    if (detach_controlling_tty_if_present() != 0) {
        return -1;
    }

    peer = alloc_session_peer(task->sid, task->pid + 100, task->pid);
    if (!peer) {
        return -1;
    }

    if (alloc_pty_pair(&master_fd, &slave_fd, &pty_index) != 0) {
        goto out;
    }
    if (pty_contract_ioctl(slave_fd, TIOCSCTTY, 0) != 0) {
        goto out;
    }
    if (pty_contract_ioctl(slave_fd, TIOCSPGRP, &peer->pgid) != 0) {
        goto out;
    }
    if (pty_contract_ioctl(slave_fd, TIOCGPGRP, &foreground_pgrp) != 0) {
        goto out;
    }
    if (foreground_pgrp != peer->pgid) {
        errno = EPROTO;
        goto out;
    }

    result = 0;

out:
    close_if_open(master_fd);
    close_if_open(slave_fd);
    if (peer) {
        task_put(peer);
    }
    task->pgid = task->pid;
    return result;
}

int pty_job_control_contract_tcsetpgrp_round_trip(void) {
    struct task *task = task_current();
    struct task *peer = NULL;
    int master_fd = -1;
    int slave_fd = -1;
    unsigned int pty_index = 0;
    __kernel_pid_t foreground_pgrp = 0;
    int result = -1;

    if (!task) {
        errno = ESRCH;
        return -1;
    }

    if (detach_controlling_tty_if_present() != 0) {
        return -1;
    }

    peer = alloc_session_peer(task->sid, task->pid + 100, task->pid);
    if (!peer) {
        return -1;
    }

    if (alloc_pty_pair(&master_fd, &slave_fd, &pty_index) != 0) {
        goto out;
    }
    if (pty_contract_ioctl(slave_fd, TIOCSCTTY, 0) != 0) {
        goto out;
    }
    if (tcsetpgrp(slave_fd, (__kernel_pid_t)peer->pgid) != 0) {
        goto out;
    }
    foreground_pgrp = tcgetpgrp(slave_fd);
    if (foreground_pgrp != (__kernel_pid_t)peer->pgid) {
        errno = EPROTO;
        goto out;
    }

    result = 0;

out:
    close_if_open(master_fd);
    close_if_open(slave_fd);
    if (peer) {
        task_put(peer);
    }
    task->pgid = task->pid;
    return result;
}

int pty_job_control_contract_background_tiocspgrp_delivers_sigttou(void) {
    struct task *task = task_current();
    struct task *foreground_peer = NULL;
    struct task *target_peer = NULL;
    int master_fd = -1;
    int slave_fd = -1;
    unsigned int pty_index = 0;
    int32_t requested_pgrp = 0;
    int saved_pgid;
    int result = -1;

    if (!task) {
        errno = ESRCH;
        return -1;
    }

    if (detach_controlling_tty_if_present() != 0) {
        return -1;
    }

    saved_pgid = task->pgid;
    task->pgid = task->pid;
    foreground_peer = alloc_session_peer(task->sid, task->pid + 100, task->pid);
    target_peer = alloc_session_peer(task->sid, task->pid + 200, task->pid);
    if (!foreground_peer || !target_peer) {
        goto out;
    }

    if (alloc_pty_pair(&master_fd, &slave_fd, &pty_index) != 0) {
        goto out;
    }
    if (pty_contract_ioctl(slave_fd, TIOCSCTTY, 0) != 0) {
        goto out;
    }
    if (pty_contract_ioctl(slave_fd, TIOCSPGRP, &foreground_peer->pgid) != 0) {
        goto out;
    }

    task->pgid = target_peer->pgid;
    requested_pgrp = target_peer->pgid;
    clear_pending_signal(task, SIGTTOU);
    errno = 0;
    if (pty_contract_ioctl(slave_fd, TIOCSPGRP, &requested_pgrp) == 0 || errno != EINTR) {
        errno = EPROTO;
        goto out;
    }
    if (!signal_is_pending(task, SIGTTOU)) {
        errno = EPROTO;
        goto out;
    }

    result = 0;

out:
    task->pgid = saved_pgid;
    close_if_open(master_fd);
    close_if_open(slave_fd);
    if (foreground_peer) {
        task_put(foreground_peer);
    }
    if (target_peer) {
        task_put(target_peer);
    }
    return result;
}

int pty_job_control_contract_background_tcsetpgrp_delivers_sigttou(void) {
    struct task *task = task_current();
    struct task *foreground_peer = NULL;
    struct task *target_peer = NULL;
    int master_fd = -1;
    int slave_fd = -1;
    unsigned int pty_index = 0;
    int saved_pgid;
    int result = -1;

    if (!task) {
        errno = ESRCH;
        return -1;
    }

    if (detach_controlling_tty_if_present() != 0) {
        return -1;
    }

    saved_pgid = task->pgid;
    task->pgid = task->pid;
    foreground_peer = alloc_session_peer(task->sid, task->pid + 100, task->pid);
    target_peer = alloc_session_peer(task->sid, task->pid + 200, task->pid);
    if (!foreground_peer || !target_peer) {
        goto out;
    }

    if (alloc_pty_pair(&master_fd, &slave_fd, &pty_index) != 0) {
        goto out;
    }
    if (pty_contract_ioctl(slave_fd, TIOCSCTTY, 0) != 0) {
        goto out;
    }
    if (tcsetpgrp(slave_fd, (__kernel_pid_t)foreground_peer->pgid) != 0) {
        goto out;
    }

    task->pgid = target_peer->pgid;
    clear_pending_signal(task, SIGTTOU);
    errno = 0;
    if (tcsetpgrp(slave_fd, (__kernel_pid_t)target_peer->pgid) == 0 || errno != EINTR) {
        errno = EPROTO;
        goto out;
    }
    if (!signal_is_pending(task, SIGTTOU)) {
        errno = EPROTO;
        goto out;
    }

    result = 0;

out:
    task->pgid = saved_pgid;
    close_if_open(master_fd);
    close_if_open(slave_fd);
    if (foreground_peer) {
        task_put(foreground_peer);
    }
    if (target_peer) {
        task_put(target_peer);
    }
    return result;
}

int pty_job_control_contract_background_read_delivers_sigttin(void) {
    struct task *task = task_current();
    struct task *foreground_peer = NULL;
    struct task *background_peer = NULL;
    int master_fd = -1;
    int slave_fd = -1;
    unsigned int pty_index = 0;
    char byte = '\0';
    int32_t background_pgid;
    int saved_pgid;
    int result = -1;

    if (!task) {
        errno = ESRCH;
        return -1;
    }

    if (detach_controlling_tty_if_present() != 0) {
        return -1;
    }

    saved_pgid = task->pgid;
    task->pgid = task->pid;
    foreground_peer = alloc_session_peer(task->sid, task->pid + 100, task->pid);
    if (!foreground_peer) {
        return -1;
    }
    background_pgid = task->pid + 200;
    background_peer = alloc_session_peer(task->sid, background_pgid, foreground_peer->pid);
    if (!background_peer) {
        goto out;
    }
    background_peer->parent = foreground_peer;

    if (alloc_pty_pair(&master_fd, &slave_fd, &pty_index) != 0) {
        goto out;
    }
    if (pty_contract_ioctl(slave_fd, TIOCSCTTY, 0) != 0) {
        goto out;
    }
    if (pty_contract_ioctl(slave_fd, TIOCSPGRP, &foreground_peer->pgid) != 0) {
        goto out;
    }

    task->pgid = background_pgid;
    clear_pending_signal(task, SIGTTIN);
    errno = 0;
    if (read_impl(slave_fd, &byte, 1) >= 0 || errno != EINTR) {
        errno = EPROTO;
        goto out;
    }
    if (!signal_is_pending(task, SIGTTIN)) {
        errno = EPROTO;
        goto out;
    }

    result = 0;

out:
    task->pgid = saved_pgid;
    close_if_open(master_fd);
    close_if_open(slave_fd);
    if (foreground_peer) {
        task_put(foreground_peer);
    }
    if (background_peer) {
        task_put(background_peer);
    }
    return result;
}

int pty_job_control_contract_background_write_delivers_sigttou(void) {
    struct task *task = task_current();
    struct task *foreground_peer = NULL;
    struct task *background_peer = NULL;
    sighandler_t saved_sigttou_handler;
    int master_fd = -1;
    int slave_fd = -1;
    unsigned int pty_index = 0;
    pty_linux_termios_t termios;
    char byte = 'x';
    int32_t background_pgid;
    int saved_pgid;
    int saved_ppid;
    struct task *saved_parent;
    int result = -1;

    if (!task) {
        errno = ESRCH;
        return -1;
    }

    if (detach_controlling_tty_if_present() != 0) {
        return -1;
    }

    saved_sigttou_handler = task->signal->actions[SIGTTOU - 1].handler;

    saved_pgid = task->pgid;
    saved_ppid = task->ppid;
    saved_parent = task->parent;
    task->pgid = task->pid;
    foreground_peer = alloc_session_peer(task->sid, task->pid + 100, task->pid);
    if (!foreground_peer) {
        return -1;
    }
    background_pgid = task->pid + 200;
    background_peer = alloc_session_peer(task->sid, background_pgid, foreground_peer->pid);
    if (!background_peer) {
        goto out;
    }
    task->ppid = foreground_peer->pid;
    task->parent = foreground_peer;
    background_peer->parent = foreground_peer;

    if (alloc_pty_pair(&master_fd, &slave_fd, &pty_index) != 0) {
        goto out;
    }
    if (pty_contract_ioctl(slave_fd, TIOCSCTTY, 0) != 0) {
        goto out;
    }
    if (pty_contract_ioctl(slave_fd, TIOCSPGRP, &foreground_peer->pgid) != 0) {
        goto out;
    }
    if (pty_get_termios_impl(pty_index, &termios) != 0) {
        goto out;
    }
    termios.c_lflag |= PTY_LFLAG_TOSTOP;
    task->pgid = foreground_peer->pgid;
    if (pty_set_termios_impl(pty_index, &termios) != 0) {
        goto out;
    }

    task->pgid = background_pgid;
    clear_pending_signal(task, SIGTTOU);
    errno = 0;
    if (write_impl(slave_fd, &byte, 1) >= 0 || errno != EINTR) {
        errno = EPROTO;
        goto out;
    }
    if (!signal_is_pending(task, SIGTTOU)) {
        errno = EPROTO;
        goto out;
    }

    result = 0;

out:
    if (task && task->signal) {
        task->signal->actions[SIGTTOU - 1].handler = saved_sigttou_handler;
    }
    task->parent = saved_parent;
    task->ppid = saved_ppid;
    task->pgid = saved_pgid;
    close_if_open(master_fd);
    close_if_open(slave_fd);
    if (foreground_peer) {
        task_put(foreground_peer);
    }
    if (background_peer) {
        task_put(background_peer);
    }
    return result;
}

int pty_job_control_contract_signal_chars_target_foreground_pgrp(void) {
    struct task *task = task_current();
    struct task *sigint_peer = NULL;
    struct task *sigquit_peer = NULL;
    struct task *sigtstp_peer = NULL;
    sighandler_t saved_sigttou_handler;
    int master_fd = -1;
    int slave_fd = -1;
    unsigned int pty_index = 0;
    unsigned char vintr = 3;
    unsigned char vquit = 28;
    unsigned char vsusp = 26;
    int result = -1;

    if (!task) {
        errno = ESRCH;
        return -1;
    }

    if (detach_controlling_tty_if_present() != 0) {
        return -1;
    }

    saved_sigttou_handler = task->signal->actions[SIGTTOU - 1].handler;
    task->signal->actions[SIGTTOU - 1].handler = SIG_IGN;

    sigint_peer = alloc_session_peer(task->sid, task->pid + 100, task->pid);
    sigquit_peer = alloc_session_peer(task->sid, task->pid + 200, task->pid);
    sigtstp_peer = alloc_session_peer(task->sid, task->pid + 300, task->pid);
    if (!sigint_peer || !sigquit_peer || !sigtstp_peer) {
        goto out;
    }

    if (alloc_pty_pair(&master_fd, &slave_fd, &pty_index) != 0) {
        goto out;
    }
    if (pty_contract_ioctl(slave_fd, TIOCSCTTY, 0) != 0) {
        goto out;
    }

    if (pty_contract_ioctl(slave_fd, TIOCSPGRP, &sigint_peer->pgid) != 0) {
        goto out;
    }
    if (pty_write_master_impl(pty_index, &vintr, 1, false) != 1) {
        goto out;
    }
    if (!signal_is_pending(sigint_peer, SIGINT) || !atomic_read(&sigint_peer->signaled) ||
        atomic_read(&sigint_peer->termsig) != SIGINT) {
        errno = EPROTO;
        goto out;
    }

    if (pty_contract_ioctl(slave_fd, TIOCSPGRP, &sigquit_peer->pgid) != 0) {
        goto out;
    }
    if (pty_write_master_impl(pty_index, &vquit, 1, false) != 1) {
        goto out;
    }
    if (!signal_is_pending(sigquit_peer, SIGQUIT)) {
        errno = EPROTO;
        goto out;
    }

    if (pty_contract_ioctl(slave_fd, TIOCSPGRP, &sigtstp_peer->pgid) != 0) {
        goto out;
    }
    if (pty_write_master_impl(pty_index, &vsusp, 1, false) != 1) {
        goto out;
    }
    if (!signal_is_pending(sigtstp_peer, SIGTSTP) ||
        atomic_read(&sigtstp_peer->state) != RUN_STATE_STOPPED) {
        errno = EPROTO;
        goto out;
    }

    result = 0;

out:
    if (task && task->signal) {
        task->signal->actions[SIGTTOU - 1].handler = saved_sigttou_handler;
    }
    close_if_open(master_fd);
    close_if_open(slave_fd);
    if (sigint_peer) {
        task_put(sigint_peer);
    }
    if (sigquit_peer) {
        task_put(sigquit_peer);
    }
    if (sigtstp_peer) {
        task_put(sigtstp_peer);
    }
    return result;
}

int pty_job_control_contract_detach_clears_dev_tty_policy(void) {
    int master_fd = -1;
    int slave_fd = -1;
    int tty_fd = -1;
    unsigned int pty_index = 0;
    int32_t foreground_pgrp = 0;
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

    tty_fd = open_impl("/dev/tty", O_RDWR, 0);
    if (tty_fd < 0) {
        goto out;
    }
    if (pty_contract_ioctl(tty_fd, TIOCNOTTY, 0) != 0) {
        goto out;
    }
    close_if_open(tty_fd);
    tty_fd = -1;

    errno = 0;
    tty_fd = open_impl("/dev/tty", O_RDWR, 0);
    if (tty_fd >= 0) {
        errno = EPROTO;
        goto out;
    }
    if (errno != ENXIO && errno != EIO) {
        errno = EPROTO;
        goto out;
    }

    errno = 0;
    if (pty_contract_ioctl(slave_fd, TIOCGPGRP, &foreground_pgrp) == 0 || errno != ENOTTY) {
        errno = EPROTO;
        goto out;
    }

    result = 0;

out:
    close_if_open(tty_fd);
    close_if_open(master_fd);
    close_if_open(slave_fd);
    return result;
}

int pty_job_control_contract_killpg_targets_process_group(void) {
    struct task *task = task_current();
    struct task *peer = NULL;
    int32_t original_pgid;
    int result = -1;

    if (!task || !task->signal) {
        errno = ESRCH;
        return -1;
    }

    original_pgid = task->pgid;
    peer = alloc_session_peer(task->sid, task->pid + 100, task->pid);
    if (!peer) {
        return -1;
    }

    task->pgid = peer->pgid;
    clear_pending_signal(task, SIGCONT);
    clear_pending_signal(peer, SIGCONT);
    atomic_set(&task->signaled, 0);
    atomic_set(&peer->signaled, 0);

    errno = 0;
    if (killpg(peer->pgid, SIGCONT) != 0) {
        goto out;
    }
    if (!signal_is_pending(task, SIGCONT) || !signal_is_pending(peer, SIGCONT)) {
        errno = EPROTO;
        goto out;
    }

    result = 0;

out:
    task->pgid = original_pgid;
    if (peer) {
        task_put(peer);
    }
    return result;
}

int pty_job_control_contract_killpg_invalid_group_returns_esrch(void) {
    errno = 0;
    if (killpg(999999, SIGCONT) == 0 || errno != ESRCH) {
        errno = EPROTO;
        return -1;
    }
    return 0;
}

static void pty_job_control_suite_init(struct kunit *test) {
    (void)test;
    reset_pty_job_control_test_kernel_state();
}

static void test_tiocspgrp_round_trip(struct kunit *test) {
    if (pty_job_control_contract_tiocspgrp_round_trip() != 0) {
        KUNIT_FAIL(test, "tiocspgrp_round_trip failed with errno %d", errno);
    }
}

static void test_tcsetpgrp_round_trip(struct kunit *test) {
    if (pty_job_control_contract_tcsetpgrp_round_trip() != 0) {
        KUNIT_FAIL(test, "tcsetpgrp_round_trip failed with errno %d", errno);
    }
}

static void test_background_tiocspgrp_delivers_sigttou(struct kunit *test) {
    if (pty_job_control_contract_background_tiocspgrp_delivers_sigttou() != 0) {
        KUNIT_FAIL(test, "background_tiocspgrp_delivers_sigttou failed with errno %d", errno);
    }
}

static void test_background_tcsetpgrp_delivers_sigttou(struct kunit *test) {
    if (pty_job_control_contract_background_tcsetpgrp_delivers_sigttou() != 0) {
        KUNIT_FAIL(test, "background_tcsetpgrp_delivers_sigttou failed with errno %d", errno);
    }
}

static void test_background_read_delivers_sigttin(struct kunit *test) {
    if (pty_job_control_contract_background_read_delivers_sigttin() != 0) {
        KUNIT_FAIL(test, "background_read_delivers_sigttin failed with errno %d", errno);
    }
}

static void test_background_write_delivers_sigttou(struct kunit *test) {
    if (pty_job_control_contract_background_write_delivers_sigttou() != 0) {
        KUNIT_FAIL(test, "background_write_delivers_sigttou failed with errno %d", errno);
    }
}

static void test_signal_chars_target_foreground_process_group(struct kunit *test) {
    if (pty_job_control_contract_signal_chars_target_foreground_pgrp() != 0) {
        KUNIT_FAIL(test, "signal_chars_target_foreground_process_group failed with errno %d", errno);
    }
}

static void test_detach_clears_dev_tty_policy(struct kunit *test) {
    if (pty_job_control_contract_detach_clears_dev_tty_policy() != 0) {
        KUNIT_FAIL(test, "detach_clears_dev_tty_policy failed with errno %d", errno);
    }
}

static void test_killpg_targets_process_group(struct kunit *test) {
    if (pty_job_control_contract_killpg_targets_process_group() != 0) {
        KUNIT_FAIL(test, "killpg_targets_process_group failed with errno %d", errno);
    }
}

static void test_killpg_invalid_group_returns_esrch(struct kunit *test) {
    if (pty_job_control_contract_killpg_invalid_group_returns_esrch() != 0) {
        KUNIT_FAIL(test, "killpg_invalid_group_returns_esrch failed with errno %d", errno);
    }
}

static void pty_job_control_suite_exit(struct kunit *test) {
    (void)test;
    reset_pty_job_control_test_kernel_state();
}

static const struct kunit_case pty_job_control_cases[] = {
    KUNIT_CASE(test_tiocspgrp_round_trip),
    KUNIT_CASE(test_tcsetpgrp_round_trip),
    KUNIT_CASE(test_background_tiocspgrp_delivers_sigttou),
    KUNIT_CASE(test_background_tcsetpgrp_delivers_sigttou),
    KUNIT_CASE(test_background_read_delivers_sigttin),
    KUNIT_CASE(test_background_write_delivers_sigttou),
    KUNIT_CASE(test_signal_chars_target_foreground_process_group),
    KUNIT_CASE(test_detach_clears_dev_tty_policy),
    KUNIT_CASE(test_killpg_targets_process_group),
    KUNIT_CASE(test_killpg_invalid_group_returns_esrch),
};

static const struct kunit_suite pty_job_control_suite = {
    .name = "pty_job_control",
    .cases = pty_job_control_cases,
    .case_count = sizeof(pty_job_control_cases) / sizeof(pty_job_control_cases[0]),
    .init = pty_job_control_suite_init,
    .exit = pty_job_control_suite_exit,
};

const struct kunit_suite *fs_pty_job_control_suite(void) {
    return &pty_job_control_suite;
}

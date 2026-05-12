/* OrlixKernel/kernel/wait.c
 * Virtual wait/waitpid implementation
 */
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/atomic.h>
#include <uapi/linux/sched.h>
#include <uapi/linux/signal.h>
#include <uapi/linux/wait.h>

#include "signal.h"
#include "../fs/fdtable.h"
#include "task.h"
#include "wait.h"

#include <linux/string.h>
enum wait_report_kind {
    WAIT_REPORT_NONE = 0,
    WAIT_REPORT_EXITED,
    WAIT_REPORT_SIGNALED,
    WAIT_REPORT_STOPPED,
    WAIT_REPORT_CONTINUED,
};

static bool wait_child_matches_selector(const struct task *parent, const struct task *child,
                                        __kernel_pid_t pid) {
    if (!parent || !child) {
        return false;
    }

    if (pid > 0) {
        return child->pid == pid;
    }
    if (pid == -1) {
        return true;
    }
    if (pid == 0) {
        return child->pgid == parent->pgid;
    }
    return child->pgid == -pid;
}

static enum wait_report_kind wait_child_report_kind(const struct task *child, int options) {
    if ((options & WUNTRACED) && atomic_read(&child->stop_report_pending)) {
        return WAIT_REPORT_STOPPED;
    }

    if (atomic_read(&child->exited)) {
        if ((options & (WEXITED | WUNTRACED | WCONTINUED)) != 0 &&
            (options & WEXITED) == 0) {
            return WAIT_REPORT_NONE;
        }
        if (atomic_read(&child->signaled)) {
            return WAIT_REPORT_SIGNALED;
        }
        return WAIT_REPORT_EXITED;
    }

    if ((options & WCONTINUED) && atomic_read(&child->continue_report_pending)) {
        return WAIT_REPORT_CONTINUED;
    }

    return WAIT_REPORT_NONE;
}

static int wait_report_status(const struct task *child, enum wait_report_kind report_kind) {
    switch (report_kind) {
    case WAIT_REPORT_EXITED:
        return (child->exit_status & 0xff) << 8;
    case WAIT_REPORT_SIGNALED:
        return atomic_read(&child->termsig) & 0x7f;
    case WAIT_REPORT_STOPPED:
        return ((int)(child->ptrace_event & 0xffff) << 16) |
               (atomic_read(&child->stopsig) << 8) | 0x7f;
    case WAIT_REPORT_CONTINUED:
        return 0xffff;
    case WAIT_REPORT_NONE:
    default:
        return 0;
    }
}

static int wait_record_restart(struct task *parent, __kernel_pid_t pid, int *wstatus, int options) {
    return task_restart_record_impl(parent, TASK_RESTART_WAITPID,
                                    (uint64_t)(int64_t)pid,
                                    (uint64_t)(uintptr_t)wstatus,
                                    (uint64_t)(int64_t)options,
                                    0, 0, 0);
}

__kernel_pid_t waitpid_impl(__kernel_pid_t pid, int *wstatus, int options) {
    struct task *parent = task_current();
    struct task *child;
    struct task *matched_child;
    enum wait_report_kind report_kind;
    bool matched_any_child;
    __kernel_pid_t matched_pid;
    int matched_status;
    bool should_reap;
    if (!parent) {
        return -ESRCH;
    }

    kernel_mutex_lock(&parent->lock);
    parent->waiters++;

    while (1) {
        matched_child = NULL;
        report_kind = WAIT_REPORT_NONE;
        matched_any_child = false;

        child = parent->children;
        while (child) {
            if ((child->clone_flags & CLONE_THREAD) != 0) {
                child = child->next_sibling;
                continue;
            }
            if (wait_child_matches_selector(parent, child, pid)) {
                matched_any_child = true;
                report_kind = wait_child_report_kind(child, options);
                if (report_kind != WAIT_REPORT_NONE) {
                    matched_child = child;
                    break;
                }
            }
            child = child->next_sibling;
        }

        if (matched_child) {
            break;
        }

        if (!matched_any_child) {
            parent->waiters--;
            kernel_mutex_unlock(&parent->lock);
            return -ECHILD;
        }

        if (options & WNOHANG) {
            parent->waiters--;
            kernel_mutex_unlock(&parent->lock);
            return 0;
        }

        if (signal_has_unblocked_pending(parent)) {
            wait_record_restart(parent, pid, wstatus, options);
            parent->waiters--;
            kernel_mutex_unlock(&parent->lock);
            return -EINTR;
        }

        kernel_cond_wait(&parent->wait_cond, &parent->lock);

        if (signal_has_unblocked_pending(parent)) {
            wait_record_restart(parent, pid, wstatus, options);
            parent->waiters--;
            kernel_mutex_unlock(&parent->lock);
            return -EINTR;
        }
    }

    matched_pid = matched_child->pid;
    matched_status = wait_report_status(matched_child, report_kind);
    should_reap = (report_kind == WAIT_REPORT_EXITED || report_kind == WAIT_REPORT_SIGNALED) &&
                  (options & WNOWAIT) == 0;

    if (report_kind == WAIT_REPORT_STOPPED && (options & WNOWAIT) == 0) {
        atomic_set(&matched_child->stop_report_pending, 0);
    } else if (report_kind == WAIT_REPORT_CONTINUED && (options & WNOWAIT) == 0) {
        atomic_set(&matched_child->continued, 0);
        atomic_set(&matched_child->continue_report_pending, 0);
    }

    if (should_reap) {
        struct task **link = &parent->children;
        while (*link && *link != matched_child) {
            link = &(*link)->next_sibling;
        }
        if (*link == matched_child) {
            *link = matched_child->next_sibling;
        }
        matched_child->next_sibling = NULL;
        if (matched_child->parent == parent) {
            matched_child->parent = NULL;
            matched_child->ppid = 0;
        }
    }

    parent->waiters--;
    kernel_mutex_unlock(&parent->lock);

    if (wstatus) {
        *wstatus = matched_status;
    }

    if (should_reap) {
        task_put(matched_child);
    }

    return matched_pid;
}

__kernel_pid_t wait4_impl(__kernel_pid_t pid, int *wstatus, int options, void *rusage) {
    /* rusage not implemented yet - just call waitpid_impl */
    (void)rusage;
    return waitpid_impl(pid, wstatus, options);
}

int waitid_impl(int idtype, __kernel_pid_t id, void *infop_arg, int options, void *rusage) {
    siginfo_t *infop = (siginfo_t *)infop_arg;
    int selector;
    int status = 0;
    __kernel_pid_t waited;
    struct task *target_task = NULL;
    struct task *parent = task_current();

    (void)rusage;
    if (!infop) {
        return -EFAULT;
    }
    if ((options & ~(WNOHANG | WEXITED | WSTOPPED | WCONTINUED | WNOWAIT)) != 0 ||
        (options & (WEXITED | WSTOPPED | WCONTINUED)) == 0) {
        return -EINVAL;
    }

    switch (idtype) {
    case P_ALL:
        selector = -1;
        break;
    case P_PID:
        if (id <= 0) {
            return -EINVAL;
        }
        selector = id;
        break;
    case P_PGID:
        selector = id == 0 ? 0 : -id;
        break;
    case P_PIDFD: {
        fd_entry_t *entry;

        entry = get_fd_entry_impl((int)id);
        if (!entry) {
            return -EBADF;
        }
        target_task = pidfd_get_task_entry_impl(entry);
        put_fd_entry_impl(entry);
        if (!target_task) {
            return -ECHILD;
        }
        if (!parent || target_task->parent != parent) {
            task_put(target_task);
            return -ECHILD;
        }
        selector = target_task->pid;
        break;
    }
    default:
        return -EINVAL;
    }

    waited = waitpid_impl(selector, &status, options);
    if (target_task) {
        task_put(target_task);
    }
    if (waited < 0) {
        return (int)waited;
    }
    memset(infop, 0, sizeof(*infop));
    if (waited == 0) {
        return 0;
    }

    infop->si_signo = SIGCHLD;
    infop->si_pid = waited;
    infop->si_uid = 0;
    if ((status & 0xffff) == 0xffff) {
        infop->si_code = CLD_CONTINUED;
        infop->si_status = SIGCONT;
    } else if ((status & 0xff) == 0x7f) {
        infop->si_code = CLD_STOPPED;
        infop->si_status = (status >> 8) & 0xff;
    } else if ((status & 0x7f) == 0) {
        infop->si_code = CLD_EXITED;
        infop->si_status = (status >> 8) & 0xff;
    } else {
        infop->si_code = CLD_KILLED;
        infop->si_status = status & 0x7f;
    }
    return 0;
}

__kernel_pid_t wait_impl(int *wstatus) {
    return waitpid_impl(-1, wstatus, 0);
}

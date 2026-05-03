/* IXLandSystem/kernel/wait.c
 * Virtual wait/waitpid implementation
 */
#include <errno.h>
#include <stdlib.h>
#include <sys/types.h>

#ifdef WSTOPPED
#undef WSTOPPED
#endif
#ifdef WCONTINUED
#undef WCONTINUED
#endif
#ifdef WNOWAIT
#undef WNOWAIT
#endif
#include <linux/wait.h>

#include "signal.h"
#include "task.h"

enum wait_report_kind {
    WAIT_REPORT_NONE = 0,
    WAIT_REPORT_EXITED,
    WAIT_REPORT_SIGNALED,
    WAIT_REPORT_STOPPED,
    WAIT_REPORT_CONTINUED,
};

static bool wait_child_matches_selector(const struct task_struct *parent, const struct task_struct *child, int32_t pid) {
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

static enum wait_report_kind wait_child_report_kind(const struct task_struct *child, int options) {
    if (atomic_load(&child->exited)) {
        if (atomic_load(&child->signaled)) {
            return WAIT_REPORT_SIGNALED;
        }
        return WAIT_REPORT_EXITED;
    }

    if ((options & WUNTRACED) && atomic_load(&child->stop_report_pending)) {
        return WAIT_REPORT_STOPPED;
    }

    if ((options & WCONTINUED) && atomic_load(&child->continue_report_pending)) {
        return WAIT_REPORT_CONTINUED;
    }

    return WAIT_REPORT_NONE;
}

static int wait_report_status(const struct task_struct *child, enum wait_report_kind report_kind) {
    switch (report_kind) {
    case WAIT_REPORT_EXITED:
        return (child->exit_status & 0xff) << 8;
    case WAIT_REPORT_SIGNALED:
        return atomic_load(&child->termsig) & 0x7f;
    case WAIT_REPORT_STOPPED:
        return (atomic_load(&child->stopsig) << 8) | 0x7f;
    case WAIT_REPORT_CONTINUED:
        return 0xffff;
    case WAIT_REPORT_NONE:
    default:
        return 0;
    }
}

static int wait_record_restart(struct task_struct *parent, int32_t pid, int *wstatus, int options) {
    return task_restart_record_impl(parent, TASK_RESTART_WAITPID,
                                    (uint64_t)(int64_t)pid,
                                    (uint64_t)(uintptr_t)wstatus,
                                    (uint64_t)(int64_t)options,
                                    0, 0, 0);
}

int32_t waitpid_impl(int32_t pid, int *wstatus, int options) {
    struct task_struct *parent = get_current();
    struct task_struct *child;
    struct task_struct *matched_child;
    enum wait_report_kind report_kind;
    bool matched_any_child;
    int32_t matched_pid;
    int matched_status;
    bool should_reap;
    if (!parent) {
        errno = ESRCH;
        return -1;
    }

    kernel_mutex_lock(&parent->lock);
    parent->waiters++;

    while (1) {
        matched_child = NULL;
        report_kind = WAIT_REPORT_NONE;
        matched_any_child = false;

        child = parent->children;
        while (child) {
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
            errno = ECHILD;
            return -1;
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
            errno = EINTR;
            return -1;
        }

        kernel_cond_wait(&parent->wait_cond, &parent->lock);

        if (signal_has_unblocked_pending(parent)) {
            wait_record_restart(parent, pid, wstatus, options);
            parent->waiters--;
            kernel_mutex_unlock(&parent->lock);
            errno = EINTR;
            return -1;
        }
    }

    matched_pid = matched_child->pid;
    matched_status = wait_report_status(matched_child, report_kind);
    should_reap = report_kind == WAIT_REPORT_EXITED || report_kind == WAIT_REPORT_SIGNALED;

    if (report_kind == WAIT_REPORT_STOPPED) {
        atomic_store(&matched_child->stop_report_pending, false);
    } else if (report_kind == WAIT_REPORT_CONTINUED) {
        atomic_store(&matched_child->continued, false);
        atomic_store(&matched_child->continue_report_pending, false);
    }

    if (should_reap) {
        struct task_struct **link = &parent->children;
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
        free_task(matched_child);
    }

    return matched_pid;
}

int32_t wait4_impl(int32_t pid, int *wstatus, int options, void *rusage) {
    /* rusage not implemented yet - just call waitpid_impl */
    (void)rusage;
    return waitpid_impl(pid, wstatus, options);
}

int32_t wait_impl(int *wstatus) {
    return waitpid_impl(-1, wstatus, 0);
}

/* ============================================================================
 * PUBLIC CANONICAL WRAPPERS
 * ============================================================================
 * These wrappers convert between POSIX/Linux public types and
 * IXLandSystem's internal representation.
 */

__attribute__((visibility("default"))) pid_t waitpid(pid_t pid, int *wstatus, int options) {
    return (pid_t)waitpid_impl((int32_t)pid, wstatus, options);
}

__attribute__((visibility("default"))) pid_t wait4(pid_t pid, int *wstatus, int options, struct rusage *rusage) {
    return (pid_t)wait4_impl((int32_t)pid, wstatus, options, (void *)rusage);
}

__attribute__((visibility("default"))) pid_t wait(int *wstatus) {
    return (pid_t)wait_impl(wstatus);
}

__attribute__((visibility("default"))) pid_t wait3(int *wstatus, int options, struct rusage *rusage) {
    return (pid_t)wait4_impl(-1, wstatus, options, (void *)rusage);
}

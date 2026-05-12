#include <linux/errno.h>

#include "futex.h"
#include "ptrace.h"
#include "signal.h"
#include "task.h"
#include "internal/exit.h"
#include "internal/kthread.h"
#include "../fs/pty.h"

struct orphaned_pgrp_candidate {
    int32_t sid;
    int32_t pgid;
};

static void task_record_orphaned_pgrp_candidate(struct orphaned_pgrp_candidate *candidates,
                                                size_t *candidate_count,
                                                int32_t sid,
                                                int32_t pgid) {
    if (!candidates || !candidate_count || sid <= 0 || pgid <= 0) {
        return;
    }

    for (size_t i = 0; i < *candidate_count; i++) {
        if (candidates[i].sid == sid && candidates[i].pgid == pgid) {
            return;
        }
    }
    if (*candidate_count < TASK_MAX_TASKS) {
        candidates[*candidate_count].sid = sid;
        candidates[*candidate_count].pgid = pgid;
        (*candidate_count)++;
    }
}

static bool task_process_group_is_orphaned_locked(int32_t sid, int32_t pgid) {
    bool has_member = false;

    if (sid <= 0 || pgid <= 0) {
        return false;
    }

    for (int i = 0; i < TASK_MAX_TASKS; i++) {
        struct task *member = task_table[i];
        while (member) {
            if (member->sid == sid && member->pgid == pgid) {
                struct task *parent = member->parent;
                has_member = true;
                if (parent && parent->sid == sid && parent->pgid != pgid) {
                    return false;
                }
            }
            member = member->hash_next;
        }
    }

    return has_member;
}

static bool task_process_group_has_stopped_member_locked(int32_t sid, int32_t pgid) {
    if (sid <= 0 || pgid <= 0) {
        return false;
    }

    for (int i = 0; i < TASK_MAX_TASKS; i++) {
        struct task *member = task_table[i];
        while (member) {
            if (member->sid == sid && member->pgid == pgid && atomic_read(&member->stopped) != 0) {
                return true;
            }
            member = member->hash_next;
        }
    }

    return false;
}

static void task_signal_newly_orphaned_stopped_groups(const struct orphaned_pgrp_candidate *candidates,
                                                      size_t candidate_count) {
    struct orphaned_pgrp_candidate notified[TASK_MAX_TASKS];
    size_t notified_count = 0;

    if (!candidates || candidate_count == 0) {
        return;
    }

    kernel_mutex_lock(&task_table_lock);
    for (size_t candidate = 0; candidate < candidate_count; candidate++) {
        int32_t sid = candidates[candidate].sid;
        int32_t pgid = candidates[candidate].pgid;
        bool seen = false;

        for (size_t i = 0; i < notified_count; i++) {
            if (notified[i].sid == sid && notified[i].pgid == pgid) {
                seen = true;
                break;
            }
        }
        if (seen || !task_process_group_is_orphaned_locked(sid, pgid) ||
            !task_process_group_has_stopped_member_locked(sid, pgid)) {
            continue;
        }
        if (notified_count < TASK_MAX_TASKS) {
            notified[notified_count].sid = sid;
            notified[notified_count].pgid = pgid;
            notified_count++;
        }
    }
    kernel_mutex_unlock(&task_table_lock);

    for (size_t i = 0; i < notified_count; i++) {
        (void)signal_generate_orphaned_pgrp(notified[i].pgid);
    }
}

void exit_impl(int status) {
    struct task *task = task_current();
    struct orphaned_pgrp_candidate orphaned_candidates[TASK_MAX_TASKS];
    size_t orphaned_candidate_count = 0;
    if (!task) {
        process_terminate(status);
    }

    futex_task_exit_impl(task);
    task_mark_exited(task, status);
    ptrace_note_exit_event(task, status);

    kernel_mutex_lock(&task->lock);

    /* Reparent children to init (orphan adoption) */
    if (task->children && task_init_process && task_init_process != task) {
        /* Lock init's children list */
        kernel_mutex_lock(&task_init_process->lock);

        /* Iterate through all children and reparent them */
        struct task *child = task->children;
        while (child) {
            kernel_mutex_lock(&child->lock);

            /* Update parent pointer and ppid */
            task_record_orphaned_pgrp_candidate(orphaned_candidates,
                                                &orphaned_candidate_count,
                                                child->sid,
                                                child->pgid);
            child->parent = task_init_process;
            child->ppid = task_init_process->pid;

            kernel_mutex_unlock(&child->lock);
            child = child->next_sibling;
        }

        /* Link entire children list to init's children list */
        /* Find the last child in our list */
        struct task *last_child = task->children;
        while (last_child->next_sibling) {
            last_child = last_child->next_sibling;
        }

        /* Prepend our children list to init's children list */
        last_child->next_sibling = task_init_process->children;
        task_init_process->children = task->children;

        /* Clear our children list */
        task->children = NULL;

        /* Wake up init if it's waiting for children */
        if (task_init_process->waiters > 0) {
            kernel_cond_broadcast(&task_init_process->wait_cond);
        }

        kernel_mutex_unlock(&task_init_process->lock);
    } else if (task->children) {
        /* No init task available, just update ppid to 1 */
        struct task *child = task->children;
        while (child) {
            kernel_mutex_lock(&child->lock);
            task_record_orphaned_pgrp_candidate(orphaned_candidates,
                                                &orphaned_candidate_count,
                                                child->sid,
                                                child->pgid);
            child->ppid = 1;
            kernel_mutex_unlock(&child->lock);
            child = child->next_sibling;
        }
    }

    /* Wake up any waiters on this task */
    if (task->waiters > 0) {
        kernel_cond_broadcast(&task->wait_cond);
    }

    kernel_mutex_unlock(&task->lock);

    /* Notify vfork parent if this is a vfork child */
    if (task->vfork_parent) {
        vfork_exit_notify();
    }

    pty_session_leader_exit_impl(task);

    if (orphaned_candidate_count > 0) {
        task_signal_newly_orphaned_stopped_groups(orphaned_candidates, orphaned_candidate_count);
    }

    task_notify_parent_state_change(task);

    /* Terminate thread but keep task until parent waits */
}

__attribute__((visibility("default"), __noreturn__)) void exit(int status) {
    exit_impl(status);
    kernel_thread_exit(NULL);
    process_terminate(status);
}

__attribute__((visibility("default"))) void _exit(int status) {
    /* Immediate exit without cleanup */
    process_terminate(status);
}

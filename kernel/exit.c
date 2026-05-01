#include <errno.h>
#include <stdlib.h>

#include "futex.h"
#include "signal.h"
#include "task.h"

/* External declaration for init task */
extern struct task_struct *init_task;

void exit_impl(int status) {
    struct task_struct *task = get_current();
    if (!task) {
        _Exit(status);
    }

    kernel_mutex_lock(&task->lock);

    futex_task_exit_impl(task);
    task_mark_exited(task, status);

    /* Reparent children to init (orphan adoption) */
    if (task->children && init_task && init_task != task) {
        /* Lock init's children list */
        kernel_mutex_lock(&init_task->lock);

        /* Iterate through all children and reparent them */
        struct task_struct *child = task->children;
        while (child) {
            kernel_mutex_lock(&child->lock);

            /* Update parent pointer and ppid */
            child->parent = init_task;
            child->ppid = init_task->pid;

            kernel_mutex_unlock(&child->lock);
            child = child->next_sibling;
        }

        /* Link entire children list to init's children list */
        /* Find the last child in our list */
        struct task_struct *last_child = task->children;
        while (last_child->next_sibling) {
            last_child = last_child->next_sibling;
        }

        /* Prepend our children list to init's children list */
        last_child->next_sibling = init_task->children;
        init_task->children = task->children;

        /* Clear our children list */
        task->children = NULL;

        /* Wake up init if it's waiting for children */
        if (init_task->waiters > 0) {
            kernel_cond_broadcast(&init_task->wait_cond);
        }

        kernel_mutex_unlock(&init_task->lock);
    } else if (task->children) {
        /* No init task available, just update ppid to 1 */
        struct task_struct *child = task->children;
        while (child) {
            kernel_mutex_lock(&child->lock);
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

    task_notify_parent_state_change(task);

    /* Terminate thread but keep task until parent waits */
}

__attribute__((visibility("default"), __noreturn__)) void exit(int status) {
    exit_impl(status);
    kernel_thread_exit(NULL);
    _Exit(status);
}

__attribute__((visibility("default"))) void _exit(int status) {
    /* Immediate exit without cleanup */
    _Exit(status);
}

#include <asm/posix_types.h>

#include <errno.h>
#include <stdatomic.h>

#include "kernel/task.h"

extern __kernel_pid_t getpgrp(void);
extern __kernel_pid_t getpgid(__kernel_pid_t pid);
extern int setpgid(__kernel_pid_t pid, __kernel_pid_t pgid);
extern __kernel_pid_t getsid(__kernel_pid_t pid);
extern __kernel_pid_t setsid(void);

static struct task_struct *create_child_task(struct task_struct *parent) {
    if (!parent) {
        errno = ESRCH;
        return NULL;
    }
    return task_create_child_impl(parent);
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

int process_group_session_contract_public_group_and_session_identity_matches_init_task(void) {
    struct task_struct *task = get_current();

    if (!task) {
        errno = ESRCH;
        return -1;
    }
    if (getpgrp() != (__kernel_pid_t)task->pgid || getsid(0) != (__kernel_pid_t)task->sid) {
        errno = EPROTO;
        return -1;
    }
    return 0;
}

int process_group_session_contract_public_getpgid_zero_matches_current_group(void) {
    struct task_struct *task = get_current();

    if (!task) {
        errno = ESRCH;
        return -1;
    }
    if (getpgid(0) != (__kernel_pid_t)task->pgid ||
        getpgid((__kernel_pid_t)task->pid) != (__kernel_pid_t)task->pgid) {
        errno = EPROTO;
        return -1;
    }
    return 0;
}

int process_group_session_contract_public_setpgid_moves_child_into_own_group(void) {
    struct task_struct *parent = get_current();
    struct task_struct *child = NULL;
    int result = -1;

    child = create_child_task(parent);
    if (!child) {
        return -1;
    }

    if (setpgid((__kernel_pid_t)child->pid, (__kernel_pid_t)child->pid) != 0) {
        goto out;
    }
    if (getpgid((__kernel_pid_t)child->pid) != (__kernel_pid_t)child->pid ||
        child->pgid != child->pid ||
        getsid((__kernel_pid_t)child->pid) != (__kernel_pid_t)parent->sid) {
        errno = EPROTO;
        goto out;
    }

    result = 0;

out:
    destroy_child_task(parent, child);
    return result;
}

int process_group_session_contract_public_setpgid_rejects_execed_child(void) {
    struct task_struct *parent = get_current();
    struct task_struct *child = NULL;
    int result = -1;

    child = create_child_task(parent);
    if (!child) {
        return -1;
    }
    atomic_store(&child->execed, true);

    errno = 0;
    if (setpgid((__kernel_pid_t)child->pid, (__kernel_pid_t)child->pid) != -1 || errno != EACCES) {
        errno = EPROTO;
        goto out;
    }

    result = 0;

out:
    destroy_child_task(parent, child);
    return result;
}

int process_group_session_contract_public_setsid_creates_new_session_for_non_leader(void) {
    struct task_struct *parent = get_current();
    struct task_struct *child = NULL;
    struct task_struct *saved_current = parent;
    __kernel_pid_t sid;
    int result = -1;

    child = create_child_task(parent);
    if (!child) {
        return -1;
    }
    if (child->pgid == child->pid) {
        errno = EPROTO;
        goto out;
    }

    set_current(child);
    sid = setsid();
    set_current(saved_current);
    if (sid != (__kernel_pid_t)child->pid) {
        errno = EPROTO;
        goto out;
    }
    if (child->sid != child->pid || child->pgid != child->pid ||
        getsid((__kernel_pid_t)child->pid) != (__kernel_pid_t)child->pid ||
        getpgid((__kernel_pid_t)child->pid) != (__kernel_pid_t)child->pid) {
        errno = EPROTO;
        goto out;
    }

    result = 0;

out:
    set_current(saved_current);
    destroy_child_task(parent, child);
    return result;
}

int process_group_session_contract_public_setpgid_rejects_session_leader(void) {
    struct task_struct *parent = get_current();
    struct task_struct *child = NULL;
    struct task_struct *saved_current = parent;
    int result = -1;

    child = create_child_task(parent);
    if (!child) {
        return -1;
    }

    set_current(child);
    if (setsid() != (__kernel_pid_t)child->pid) {
        set_current(saved_current);
        goto out;
    }
    set_current(saved_current);

    errno = 0;
    if (setpgid((__kernel_pid_t)child->pid, (__kernel_pid_t)parent->pgid) != -1 || errno != EPERM) {
        errno = EPROTO;
        goto out;
    }

    result = 0;

out:
    set_current(saved_current);
    destroy_child_task(parent, child);
    return result;
}

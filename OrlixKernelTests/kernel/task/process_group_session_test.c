#include <asm/posix_types.h>
#include <uapi/linux/errno.h>

#include "../../kunit/kunit.h"
#include "../../kunit/suite_registry.h"
#include "kernel/init.h"
#include "kernel/task.h"

extern int errno;

extern __kernel_pid_t getpgrp(void);
extern __kernel_pid_t getpgid(__kernel_pid_t pid);
extern int setpgid(__kernel_pid_t pid, __kernel_pid_t pgid);
extern __kernel_pid_t getsid(__kernel_pid_t pid);
extern __kernel_pid_t setsid(void);

static struct task *create_child_task(struct task *parent) {
    if (!parent) {
        errno = ESRCH;
        return NULL;
    }
    return task_create_child_impl(parent);
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

static void reset_process_group_session_test_kernel_state(void) {
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
    atomic_set(&task_init_process->execed, 0);

    while ((child = task_init_process->children) != NULL) {
        task_unlink_child_impl(task_init_process, child);
        child->parent = NULL;
        child->ppid = 0;
        task_put(child);
    }
}

int process_group_session_contract_public_group_and_session_identity_matches_init_task(void) {
    struct task *task = task_current();

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
    struct task *task = task_current();

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
    struct task *parent = task_current();
    struct task *child = NULL;
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
    struct task *parent = task_current();
    struct task *child = NULL;
    int result = -1;

    child = create_child_task(parent);
    if (!child) {
        return -1;
    }
    atomic_set(&child->execed, 1);

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
    struct task *parent = task_current();
    struct task *child = NULL;
    struct task *saved_current = parent;
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

    task_set_current(child);
    sid = setsid();
    task_set_current(saved_current);
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
    task_set_current(saved_current);
    destroy_child_task(parent, child);
    return result;
}

int process_group_session_contract_public_setpgid_rejects_session_leader(void) {
    struct task *parent = task_current();
    struct task *child = NULL;
    struct task *saved_current = parent;
    int result = -1;

    child = create_child_task(parent);
    if (!child) {
        return -1;
    }

    task_set_current(child);
    if (setsid() != (__kernel_pid_t)child->pid) {
        task_set_current(saved_current);
        goto out;
    }
    task_set_current(saved_current);

    errno = 0;
    if (setpgid((__kernel_pid_t)child->pid, (__kernel_pid_t)parent->pgid) != -1 || errno != EPERM) {
        errno = EPROTO;
        goto out;
    }

    result = 0;

out:
    task_set_current(saved_current);
    destroy_child_task(parent, child);
    return result;
}

static void process_group_session_suite_init(struct kunit *test) {
    (void)test;
    reset_process_group_session_test_kernel_state();
}

static void process_group_session_suite_exit(struct kunit *test) {
    (void)test;
    reset_process_group_session_test_kernel_state();
}

static void test_group_and_session_identity_matches_init_task(struct kunit *test) {
    if (process_group_session_contract_public_group_and_session_identity_matches_init_task() != 0) {
        KUNIT_FAIL(test, "group_and_session_identity_matches_init_task failed with errno %d", errno);
    }
}

static void test_getpgid_zero_matches_current_group(struct kunit *test) {
    if (process_group_session_contract_public_getpgid_zero_matches_current_group() != 0) {
        KUNIT_FAIL(test, "getpgid_zero_matches_current_group failed with errno %d", errno);
    }
}

static void test_setpgid_moves_child_into_own_group(struct kunit *test) {
    if (process_group_session_contract_public_setpgid_moves_child_into_own_group() != 0) {
        KUNIT_FAIL(test, "setpgid_moves_child_into_own_group failed with errno %d", errno);
    }
}

static void test_setpgid_rejects_execed_child(struct kunit *test) {
    if (process_group_session_contract_public_setpgid_rejects_execed_child() != 0) {
        KUNIT_FAIL(test, "setpgid_rejects_execed_child failed with errno %d", errno);
    }
}

static void test_setsid_creates_new_session_for_non_leader(struct kunit *test) {
    if (process_group_session_contract_public_setsid_creates_new_session_for_non_leader() != 0) {
        KUNIT_FAIL(test, "setsid_creates_new_session_for_non_leader failed with errno %d", errno);
    }
}

static void test_setpgid_rejects_session_leader(struct kunit *test) {
    if (process_group_session_contract_public_setpgid_rejects_session_leader() != 0) {
        KUNIT_FAIL(test, "setpgid_rejects_session_leader failed with errno %d", errno);
    }
}

static const struct kunit_case process_group_session_cases[] = {
    KUNIT_CASE(test_group_and_session_identity_matches_init_task),
    KUNIT_CASE(test_getpgid_zero_matches_current_group),
    KUNIT_CASE(test_setpgid_moves_child_into_own_group),
    KUNIT_CASE(test_setpgid_rejects_execed_child),
    KUNIT_CASE(test_setsid_creates_new_session_for_non_leader),
    KUNIT_CASE(test_setpgid_rejects_session_leader),
};

static const struct kunit_suite process_group_session_suite = {
    .name = "process_group_session",
    .cases = process_group_session_cases,
    .case_count = sizeof(process_group_session_cases) / sizeof(process_group_session_cases[0]),
    .init = process_group_session_suite_init,
    .exit = process_group_session_suite_exit,
};

const struct kunit_suite *kernel_process_group_session_suite(void) {
    return &process_group_session_suite;
}

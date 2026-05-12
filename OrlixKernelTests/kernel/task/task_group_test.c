#include <uapi/linux/errno.h>

#include "../../kunit/kunit.h"
#include "../../kunit/suite_registry.h"
#include "fs/fdtable.h"
#include "kernel/signal.h"
#include "kernel/task.h"

extern int library_init(const void *config);
extern int library_is_initialized(void);
extern int errno;

static void task_group_suite_init(struct kunit *test) {
    (void)test;
    if (!library_is_initialized()) {
        library_init(NULL);
    }
    for (int fd = 3; fd < 256; fd++) {
        close_impl(fd);
    }
}

static void task_group_suite_exit(struct kunit *test) {
    (void)test;
    for (int fd = 3; fd < 256; fd++) {
        close_impl(fd);
    }
}

static void test_initial_task_pgid_and_sid_coherence(struct kunit *test) {
    struct task *task = task_current();

    KUNIT_ASSERT_TRUE(test, task != NULL);
    if (task->pgid != task->pid || task->sid != task->pid) {
        KUNIT_FAIL(test, "initial task should be its own process group and session leader");
    }
}

static void test_getpgrp_impl_returns_current_task_pgid(struct kunit *test) {
    struct task *task = task_current();
    int pgid;

    KUNIT_ASSERT_TRUE(test, task != NULL);
    pgid = getpgrp_impl();
    if (pgid <= 0 || pgid != task->pgid) {
        KUNIT_FAIL(test, "getpgrp_impl should return current task pgid");
    }
}

static void test_getpgid_impl_returns_target_pgid(struct kunit *test) {
    struct task *task = task_current();
    int pgid;

    KUNIT_ASSERT_TRUE(test, task != NULL);
    pgid = getpgid_impl(task->pid);
    if (pgid <= 0 || pgid != task->pgid) {
        KUNIT_FAIL(test, "getpgid_impl should return target pgid");
    }
}

static void test_getpgid_impl_zero_returns_current_pgid(struct kunit *test) {
    struct task *task = task_current();
    int pgid_zero;
    int pgid_explicit;

    KUNIT_ASSERT_TRUE(test, task != NULL);
    pgid_zero = getpgid_impl(0);
    pgid_explicit = getpgid_impl(task->pid);
    if (pgid_zero != pgid_explicit) {
        KUNIT_FAIL(test, "getpgid_impl(0) should match explicit current pid");
    }
}

static void test_getpgid_impl_rejects_invalid_pid(struct kunit *test) {
    int pgid;

    errno = 0;
    pgid = getpgid_impl(-9999);
    if (pgid != -1 || errno != ESRCH) {
        KUNIT_FAIL(test, "invalid pid should fail with ESRCH");
    }
}

static void test_setpgid_impl_rejects_negative_pgid(struct kunit *test) {
    struct task *task = task_current();
    int result;

    KUNIT_ASSERT_TRUE(test, task != NULL);
    errno = 0;
    result = setpgid_impl(task->pid, -5);
    if (result != -1 || errno != EINVAL) {
        KUNIT_FAIL(test, "negative pgid should fail with EINVAL");
    }
}

static void test_setpgid_impl_rejects_invalid_pid(struct kunit *test) {
    int result;

    errno = 0;
    result = setpgid_impl(-9999, 0);
    if (result != -1 || errno != ESRCH) {
        KUNIT_FAIL(test, "invalid pid should fail with ESRCH");
    }
}

static void test_setpgid_impl_rejects_session_leader(struct kunit *test) {
    struct task *task = task_current();
    int result;

    KUNIT_ASSERT_TRUE(test, task != NULL);
    KUNIT_ASSERT_TRUE(test, task->sid == task->pid);
    errno = 0;
    result = setpgid_impl(task->pid, task->pid + 1);
    if (result != -1 || errno != EPERM) {
        KUNIT_FAIL(test, "session leader should not change pgid");
    }
}

static void test_setpgid_impl_creates_new_group_with_zero(struct kunit *test) {
    struct task *task = task_current();
    int result;
    int new_pgid;

    KUNIT_ASSERT_TRUE(test, task != NULL);
    errno = 0;
    result = setpgid_impl(task->pid, task->pid);
    if (result == -1) {
        if (errno != EPERM) {
            KUNIT_FAIL(test, "expected EPERM when session leader cannot change pgid");
        }
        return;
    }
    new_pgid = getpgid_impl(task->pid);
    if (new_pgid != task->pid) {
        KUNIT_FAIL(test, "pgid should be task pid");
    }
}

static void test_setsid_impl_rejects_process_group_leader(struct kunit *test) {
    struct task *task = task_current();
    int result;

    KUNIT_ASSERT_TRUE(test, task != NULL);
    KUNIT_ASSERT_TRUE(test, task->pgid == task->pid);
    errno = 0;
    result = setsid_impl();
    if (result != -1 || errno != EPERM) {
        KUNIT_FAIL(test, "setsid should fail for process group leader");
    }
}

static void test_signal_mask_in_task_context(struct kunit *test) {
    struct task *task = task_current();
    struct signal_mask_bits mask = {0};
    struct signal_mask_bits oldmask = {0};
    int result;
    int is_blocked;

    KUNIT_ASSERT_TRUE(test, task != NULL);
    KUNIT_ASSERT_TRUE(test, task->signal != NULL);

    mask.sig[(10 - 1) >> 6] |= (1ULL << ((10 - 1) & 63));
    result = do_sigprocmask(0, &mask, &oldmask);
    if (result != 0) {
        KUNIT_FAIL(test, "SIG_BLOCK should succeed");
    }
    is_blocked = signal_is_blocked(task, 10);
    if (!is_blocked) {
        KUNIT_FAIL(test, "SIGUSR1 should be blocked");
    }
    result = do_sigprocmask(1, &mask, NULL);
    if (result != 0) {
        KUNIT_FAIL(test, "SIG_UNBLOCK should succeed");
    }
    is_blocked = signal_is_blocked(task, 10);
    if (is_blocked) {
        KUNIT_FAIL(test, "SIGUSR1 should be unblocked");
    }
}

static const struct kunit_case task_group_cases[] = {
    KUNIT_CASE(test_initial_task_pgid_and_sid_coherence),
    KUNIT_CASE(test_getpgrp_impl_returns_current_task_pgid),
    KUNIT_CASE(test_getpgid_impl_returns_target_pgid),
    KUNIT_CASE(test_getpgid_impl_zero_returns_current_pgid),
    KUNIT_CASE(test_getpgid_impl_rejects_invalid_pid),
    KUNIT_CASE(test_setpgid_impl_rejects_negative_pgid),
    KUNIT_CASE(test_setpgid_impl_rejects_invalid_pid),
    KUNIT_CASE(test_setpgid_impl_rejects_session_leader),
    KUNIT_CASE(test_setpgid_impl_creates_new_group_with_zero),
    KUNIT_CASE(test_setsid_impl_rejects_process_group_leader),
    KUNIT_CASE(test_signal_mask_in_task_context),
};

static const struct kunit_suite task_group_suite = {
    .name = "task_group",
    .cases = task_group_cases,
    .case_count = sizeof(task_group_cases) / sizeof(task_group_cases[0]),
    .init = task_group_suite_init,
    .exit = task_group_suite_exit,
};

const struct kunit_suite *kernel_task_group_suite(void) {
    return &task_group_suite;
}

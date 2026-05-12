#include <uapi/linux/errno.h>
#include <uapi/linux/signal.h>
#include <linux/string.h>

#include "../../kunit/kunit.h"
#include "../../kunit/suite_registry.h"
#include "fs/fdtable.h"
#include "kernel/signal.h"
#include "private/kernel/signal_state.h"
#include "kernel/task.h"
#include "private/kernel/task_state.h"

extern int signal_syscall_contract_pidfd_send_signal_obeys_linux_targeting_rules(void);
extern int signal_syscall_contract_pidfd_send_signal_rejects_invalid_parameters(void);
extern int library_init(const void *config);
extern int library_is_initialized(void);
extern int errno;

static void signal_suite_init(struct kunit *test) {
    (void)test;
    if (!library_is_initialized()) {
        library_init(NULL);
    }
    for (int fd = 3; fd < 256; fd++) {
        close_impl(fd);
    }
}

static void signal_suite_exit(struct kunit *test) {
    (void)test;
    for (int fd = 3; fd < 256; fd++) {
        close_impl(fd);
    }
}

static void test_library_initialization(struct kunit *test) {
    if (!library_is_initialized()) {
        KUNIT_FAIL(test, "library should be initialized");
    }
}

static void test_do_sigprocmask_basic_operations(struct kunit *test) {
    struct task *task = task_current();
    sigset_t mask = {0};
    sigset_t oldmask = {0};
    sigset_t queried = {0};
    int result;
    int is_blocked;

    KUNIT_ASSERT_TRUE(test, task != NULL);
    KUNIT_ASSERT_TRUE(test, task->signal != NULL);

    sigaddset(&mask, 10);
    result = do_sigprocmask(SIG_BLOCK, &mask, &oldmask);
    if (result != 0) {
        KUNIT_FAIL(test, "SIG_BLOCK should succeed");
    }
    result = do_sigprocmask(SIG_BLOCK, NULL, &queried);
    if (result != 0) {
        KUNIT_FAIL(test, "query should succeed");
    }
    is_blocked = signal_is_blocked(task, 10);
    if (!is_blocked) {
        KUNIT_FAIL(test, "SIGUSR1 should be blocked");
    }
    result = do_sigprocmask(SIG_UNBLOCK, &mask, NULL);
    if (result != 0) {
        KUNIT_FAIL(test, "SIG_UNBLOCK should succeed");
    }
    is_blocked = signal_is_blocked(task, 10);
    if (is_blocked) {
        KUNIT_FAIL(test, "SIGUSR1 should be unblocked");
    }
}

static void test_do_sigprocmask_invalid_how(struct kunit *test) {
    sigset_t mask = {0};
    sigset_t oldmask = {0};
    int result;

    errno = 0;
    result = do_sigprocmask(999, &mask, &oldmask);
    if (result != -1 || errno != EINVAL) {
        KUNIT_FAIL(test, "invalid how should fail with EINVAL");
    }
}

static void test_do_raise_signal_to_self(struct kunit *test) {
    struct task *task = task_current();
    sigset_t mask = {0};
    sigset_t oldmask = {0};
    sigset_t pending = {0};
    int result;
    int is_pending;

    KUNIT_ASSERT_TRUE(test, task != NULL);
    KUNIT_ASSERT_TRUE(test, task->signal != NULL);

    sigaddset(&mask, 10);
    result = do_sigprocmask(SIG_BLOCK, &mask, &oldmask);
    if (result != 0) {
        KUNIT_FAIL(test, "block SIGUSR1 should succeed");
    }

    task->thread_pending_signals = 0;
    memset(&task->signal->shared_pending, 0, sizeof(task->signal->shared_pending));

    result = do_raise(10);
    if (result != 0) {
        KUNIT_FAIL(test, "do_raise should succeed");
    }
    result = do_sigpending(&pending);
    if (result != 0) {
        KUNIT_FAIL(test, "do_sigpending should succeed");
    }

    is_pending = sigismember(&pending, 10) != 0;
    if (!is_pending) {
        KUNIT_FAIL(test, "SIGUSR1 should be pending");
    }
    do_sigprocmask(SIG_SETMASK, &oldmask, NULL);
}

static void test_do_kill_signal_to_current_task(struct kunit *test) {
    struct task *task = task_current();
    sigset_t mask = {0};
    sigset_t oldmask = {0};
    sigset_t pending = {0};
    int result;
    int is_pending;

    KUNIT_ASSERT_TRUE(test, task != NULL);
    KUNIT_ASSERT_TRUE(test, task->signal != NULL);

    sigaddset(&mask, 10);
    result = do_sigprocmask(SIG_BLOCK, &mask, &oldmask);
    if (result != 0) {
        KUNIT_FAIL(test, "block SIGUSR1 should succeed");
    }

    task->thread_pending_signals = 0;
    memset(&task->signal->shared_pending, 0, sizeof(task->signal->shared_pending));

    result = do_kill(task->pid, 10);
    if (result != 0) {
        KUNIT_FAIL(test, "do_kill should succeed");
    }
    result = do_sigpending(&pending);
    if (result != 0) {
        KUNIT_FAIL(test, "do_sigpending should succeed");
    }

    is_pending = sigismember(&pending, 10) != 0;
    if (!is_pending) {
        KUNIT_FAIL(test, "SIGUSR1 should be pending after do_kill");
    }
    do_sigprocmask(SIG_SETMASK, &oldmask, NULL);
}

static void test_do_sigaction_basic_operations(struct kunit *test) {
    struct task *task = task_current();
    struct sigaction new_act = {0};
    struct sigaction old_act = {0};
    int result;

    KUNIT_ASSERT_TRUE(test, task != NULL);
    KUNIT_ASSERT_TRUE(test, task->signal != NULL);

    new_act.sa_handler = SIG_DFL;
    sigemptyset(&new_act.sa_mask);

    result = do_sigaction(10, &new_act, &old_act);
    if (result != 0) {
        KUNIT_FAIL(test, "do_sigaction should succeed");
    }
    result = do_sigaction(10, NULL, &old_act);
    if (result != 0) {
        KUNIT_FAIL(test, "do_sigaction query should succeed");
    }
    if (old_act.sa_handler != SIG_DFL) {
        KUNIT_FAIL(test, "handler should be SIG_DFL");
    }
}

static void test_do_sigaction_ignores_unblockable_signals(struct kunit *test) {
    struct sigaction new_act = {0};
    struct sigaction old_act = {0};
    int result;

    new_act.sa_handler = SIG_DFL;
    errno = 0;
    result = do_sigaction(9, &new_act, &old_act);
    if (result != -1 || errno != EINVAL) {
        KUNIT_FAIL(test, "SIGKILL should fail with EINVAL");
    }
    errno = 0;
    result = do_sigaction(19, &new_act, &old_act);
    if (result != -1 || errno != EINVAL) {
        KUNIT_FAIL(test, "SIGSTOP should fail with EINVAL");
    }
}

static void test_do_sigpending_basic_operations(struct kunit *test) {
    struct task *task = task_current();
    sigset_t mask = {0};
    sigset_t oldmask = {0};
    sigset_t pending = {0};
    int result;
    int has_pending;

    KUNIT_ASSERT_TRUE(test, task != NULL);
    KUNIT_ASSERT_TRUE(test, task->signal != NULL);

    sigaddset(&mask, 12);
    result = do_sigprocmask(SIG_BLOCK, &mask, &oldmask);
    if (result != 0) {
        KUNIT_FAIL(test, "block SIGUSR2 should succeed");
    }

    task->thread_pending_signals = 0;
    memset(&task->signal->shared_pending, 0, sizeof(task->signal->shared_pending));

    result = do_sigpending(&pending);
    if (result != 0) {
        KUNIT_FAIL(test, "do_sigpending should succeed");
    }
    has_pending = sigismember(&pending, 12) != 0;
    if (has_pending) {
        KUNIT_FAIL(test, "no signal should be pending initially");
    }

    result = do_raise(12);
    if (result != 0) {
        KUNIT_FAIL(test, "do_raise SIGUSR2 should succeed");
    }
    result = do_sigpending(&pending);
    if (result != 0) {
        KUNIT_FAIL(test, "do_sigpending should succeed after raise");
    }
    has_pending = sigismember(&pending, 12) != 0;
    if (!has_pending) {
        KUNIT_FAIL(test, "SIGUSR2 should be pending");
    }
    do_sigprocmask(SIG_SETMASK, &oldmask, NULL);
}

static void test_signal_set_operations(struct kunit *test) {
    sigset_t set = {0};
    int is_member;

    is_member = kernel_sigismember(&set, 10);
    if (is_member) {
        KUNIT_FAIL(test, "SIGUSR1 should not be in empty set");
    }
    kernel_sigaddset(&set, 10);
    if (!kernel_sigismember(&set, 10)) {
        KUNIT_FAIL(test, "SIGUSR1 should be in set after add");
    }
    kernel_sigaddset(&set, 12);
    if (!kernel_sigismember(&set, 12)) {
        KUNIT_FAIL(test, "SIGUSR2 should be in set after add");
    }
    sigdelset(&set, 10);
    if (kernel_sigismember(&set, 10)) {
        KUNIT_FAIL(test, "SIGUSR1 should not be in set after del");
    }
    if (!kernel_sigismember(&set, 12)) {
        KUNIT_FAIL(test, "SIGUSR2 should still be in set");
    }
    sigfillset(&set);
    if (!kernel_sigismember(&set, SIGCHLD)) {
        KUNIT_FAIL(test, "SIGCHLD should be in filled set");
    }
    kernel_sigemptyset(&set);
    if (kernel_sigismember(&set, 10)) {
        KUNIT_FAIL(test, "SIGUSR1 should not be in empty set after sigemptyset");
    }
}

static void test_signal_is_blocked(struct kunit *test) {
    struct task *task = task_current();
    sigset_t mask = {0};
    sigset_t oldmask = {0};
    int result;
    int is_blocked;

    KUNIT_ASSERT_TRUE(test, task != NULL);

    is_blocked = signal_is_blocked(task, SIGCHLD);
    if (is_blocked) {
        KUNIT_FAIL(test, "SIGCHLD should not be blocked initially");
    }
    sigaddset(&mask, SIGCHLD);
    result = do_sigprocmask(SIG_BLOCK, &mask, &oldmask);
    if (result != 0) {
        KUNIT_FAIL(test, "block SIGCHLD should succeed");
    }
    is_blocked = signal_is_blocked(task, SIGCHLD);
    if (!is_blocked) {
        KUNIT_FAIL(test, "SIGCHLD should be blocked");
    }
    if (signal_is_blocked(task, SIGUSR1)) {
        KUNIT_FAIL(test, "SIGUSR1 should not be blocked");
    }
    do_sigprocmask(SIG_SETMASK, &oldmask, NULL);
}

static void test_pidfd_send_signal_uses_process_directed_linux_semantics(struct kunit *test) {
    if (signal_syscall_contract_pidfd_send_signal_obeys_linux_targeting_rules() != 0) {
        KUNIT_FAIL(test, "pidfd_send_signal Linux targeting rules failed with errno %d", errno);
    }
}

static void test_pidfd_send_signal_rejects_invalid_parameters(struct kunit *test) {
    if (signal_syscall_contract_pidfd_send_signal_rejects_invalid_parameters() != 0) {
        KUNIT_FAIL(test, "pidfd_send_signal invalid parameter checks failed with errno %d", errno);
    }
}

static const struct kunit_case signal_cases[] = {
    KUNIT_CASE(test_library_initialization),
    KUNIT_CASE(test_do_sigprocmask_basic_operations),
    KUNIT_CASE(test_do_sigprocmask_invalid_how),
    KUNIT_CASE(test_do_raise_signal_to_self),
    KUNIT_CASE(test_do_kill_signal_to_current_task),
    KUNIT_CASE(test_do_sigaction_basic_operations),
    KUNIT_CASE(test_do_sigaction_ignores_unblockable_signals),
    KUNIT_CASE(test_do_sigpending_basic_operations),
    KUNIT_CASE(test_signal_set_operations),
    KUNIT_CASE(test_signal_is_blocked),
    KUNIT_CASE(test_pidfd_send_signal_uses_process_directed_linux_semantics),
    KUNIT_CASE(test_pidfd_send_signal_rejects_invalid_parameters),
};

static const struct kunit_suite signal_suite = {
    .name = "signal",
    .cases = signal_cases,
    .case_count = sizeof(signal_cases) / sizeof(signal_cases[0]),
    .init = signal_suite_init,
    .exit = signal_suite_exit,
};

const struct kunit_suite *kernel_signal_suite(void) {
    return &signal_suite;
}

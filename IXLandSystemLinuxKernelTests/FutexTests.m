#import <XCTest/XCTest.h>

#include <errno.h>

#include "FutexContract.h"
#include "kernel/init.h"
#include "kernel/signal.h"
#include "kernel/task.h"

static void reset_futex_test_kernel_state(void) {
    struct task_struct *child;

    XCTAssertEqual(start_kernel(), 0, @"start_kernel must succeed before futex tests");
    XCTAssertTrue(kernel_is_booted(), @"kernel must be booted before futex tests");
    XCTAssertNotEqual(init_task, NULL, @"init_task must exist before futex tests");

    if (!init_task) {
        return;
    }

    set_current(init_task);
    init_task->parent = NULL;
    init_task->ppid = 0;
    init_task->exit_status = 0;
    init_task->thread_pending_signals = 0;
    atomic_store(&init_task->exited, false);
    atomic_store(&init_task->signaled, false);
    atomic_store(&init_task->termsig, 0);
    atomic_store(&init_task->stopped, false);
    atomic_store(&init_task->state, TASK_RUNNING);
    atomic_store(&init_task->continued, false);
    atomic_store(&init_task->stop_report_pending, false);
    atomic_store(&init_task->continue_report_pending, false);

    if (init_task->signal) {
        memset(&init_task->signal->blocked, 0, sizeof(init_task->signal->blocked));
        memset(&init_task->signal->pending, 0, sizeof(init_task->signal->pending));
        memset(&init_task->signal->shared_pending, 0, sizeof(init_task->signal->shared_pending));
    }

    while ((child = init_task->children) != NULL) {
        task_unlink_child_impl(init_task, child);
        child->parent = NULL;
        child->ppid = 0;
    }

    set_current(init_task);
}

extern int library_init(const void *config);
extern int library_is_initialized(void);

@interface FutexTests : XCTestCase
@end

@implementation FutexTests

- (void)setUp {
    [super setUp];
    if (!library_is_initialized()) {
        library_init(NULL);
    }
    reset_futex_test_kernel_state();
}

- (void)tearDown {
    reset_futex_test_kernel_state();
    [super tearDown];
}

- (void)testFutexWaitMismatchReturnsAgain {
    XCTAssertEqual(futex_contract_wait_mismatch_returns_again(), 0, @"errno %d", errno);
}

- (void)testFutexWakeWithoutWaitersReturnsZero {
    XCTAssertEqual(futex_contract_wake_without_waiters_returns_zero(), 0, @"errno %d", errno);
}

- (void)testFutexWaitTimeoutReturnsTimedout {
    XCTAssertEqual(futex_contract_wait_timeout_returns_timedout(), 0, @"errno %d", errno);
}

- (void)testFutexWakeReleasesWaiter {
    XCTAssertEqual(futex_contract_wake_releases_waiter(), 0, @"errno %d", errno);
}

- (void)testFutexInterruptedWaitRecordsRestart {
    XCTAssertEqual(futex_contract_interrupted_wait_records_restart(), 0, @"errno %d", errno);
}

- (void)testFutexSetsAndGetsRobustList {
    XCTAssertEqual(futex_contract_sets_and_gets_robust_list(), 0, @"errno %d", errno);
}

- (void)testFutexRejectsMissingRobustListOutputs {
    XCTAssertEqual(futex_contract_rejects_missing_robust_list_outputs(), 0, @"errno %d", errno);
}

- (void)testFutexExitClearsChildTidAndMarksRobustFutex {
    XCTAssertEqual(futex_contract_exit_clears_child_tid_and_marks_robust_futex(), 0, @"errno %d", errno);
}

- (void)testCloneThreadSharesVmAndThreadGroup {
    XCTAssertEqual(futex_contract_clone_thread_shares_vm_and_thread_group(), 0, @"errno %d", errno);
}

- (void)testClone3SetsParentChildAndClearTid {
    XCTAssertEqual(futex_contract_clone3_sets_parent_child_and_clear_tid(), 0, @"errno %d", errno);
}

- (void)testClone3SetTidSupportsRepoPidModel {
    XCTAssertEqual(futex_contract_clone3_set_tid_supports_repo_pid_model(), 0, @"errno %d", errno);
}

- (void)testClearChildTidIsPerThreadNotMmShared {
    XCTAssertEqual(futex_contract_clear_child_tid_is_per_thread_not_mm_shared(), 0, @"errno %d", errno);
}

@end

#import <XCTest/XCTest.h>

#include <errno.h>
#include <string.h>

#include "kernel/init.h"
#include "kernel/signal.h"
#include "kernel/task.h"
#include "WaitJobControlContract.h"

static void reset_wait_job_control_test_kernel_state(void) {
    struct task_struct *child;

    XCTAssertEqual(start_kernel(), 0, @"start_kernel must succeed before wait job-control tests");
    XCTAssertTrue(kernel_is_booted(), @"kernel must be booted before wait job-control tests");
    XCTAssertNotEqual(init_task, NULL, @"init_task must exist before wait job-control tests");

    if (!init_task) {
        return;
    }

    set_current(init_task);
    init_task->parent = NULL;
    init_task->ppid = 0;
    init_task->pgid = init_task->pid;
    init_task->sid = init_task->pid;
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

@interface WaitJobControlTests : XCTestCase
@end

@implementation WaitJobControlTests

- (void)setUp {
    [super setUp];
    reset_wait_job_control_test_kernel_state();
}

- (void)tearDown {
    reset_wait_job_control_test_kernel_state();
    [super tearDown];
}

- (void)testWaitpidNoChildrenReturnsEchild {
    XCTAssertEqual(wait_job_control_contract_no_children_returns_echild(), 0, @"errno %d", errno);
}

- (void)testWaitpidSpecificNonChildReturnsEchild {
    XCTAssertEqual(wait_job_control_contract_specific_non_child_returns_echild(), 0, @"errno %d", errno);
}

- (void)testWaitpidWnohangReturnsZeroForRunningChild {
    XCTAssertEqual(wait_job_control_contract_wnohang_returns_zero_for_running_child(), 0, @"errno %d", errno);
}

- (void)testWaitpidReapsExitedChild {
    XCTAssertEqual(wait_job_control_contract_reaps_exited_child(), 0, @"errno %d", errno);
}

- (void)testWaitpidSecondWaitAfterReapReturnsEchild {
    XCTAssertEqual(wait_job_control_contract_second_wait_after_reap_returns_echild(), 0, @"errno %d", errno);
}

- (void)testWaitpidNullStatusStillReapsExitedChild {
    XCTAssertEqual(wait_job_control_contract_null_status_still_reaps_exited_child(), 0, @"errno %d", errno);
}

- (void)testWaitpidReportsStoppedChildWithWuntraced {
    XCTAssertEqual(wait_job_control_contract_reports_stopped_child_with_wuntraced(), 0, @"errno %d", errno);
}

- (void)testWaitpidStoppedChildWithoutWuntracedWnohangReturnsZero {
    XCTAssertEqual(wait_job_control_contract_stopped_child_without_wuntraced_wnohang_returns_zero(), 0, @"errno %d", errno);
}

- (void)testWaitpidStoppedChildIsNotReaped {
    XCTAssertEqual(wait_job_control_contract_stopped_child_is_not_reaped(), 0, @"errno %d", errno);
}

- (void)testWaitpidReportsContinuedChildWithWcontinued {
    XCTAssertEqual(wait_job_control_contract_reports_continued_child_with_wcontinued(), 0, @"errno %d", errno);
}

- (void)testWaitpidContinuedStatusIsLinuxWifcontinued {
    XCTAssertEqual(wait_job_control_contract_continued_status_is_linux_wifcontinued(), 0, @"errno %d", errno);
}

- (void)testWaitpidContinuedReportIsConsumed {
    XCTAssertEqual(wait_job_control_contract_continued_report_is_consumed(), 0, @"errno %d", errno);
}

- (void)testWaitpidPidZeroSelectsSameProcessGroup {
    XCTAssertEqual(wait_job_control_contract_pid_zero_selects_same_process_group(), 0, @"errno %d", errno);
}

- (void)testWaitpidNegativePidSelectsProcessGroup {
    XCTAssertEqual(wait_job_control_contract_negative_pid_selects_process_group(), 0, @"errno %d", errno);
}

- (void)testChildStopGeneratesSigchldForParent {
    XCTAssertEqual(wait_job_control_contract_child_stop_generates_sigchld_for_parent(), 0, @"errno %d", errno);
}

- (void)testChildContinueGeneratesSigchldForParent {
    XCTAssertEqual(wait_job_control_contract_child_continue_generates_sigchld_for_parent(), 0, @"errno %d", errno);
}

- (void)testChildExitGeneratesSigchldForParent {
    XCTAssertEqual(wait_job_control_contract_child_exit_generates_sigchld_for_parent(), 0, @"errno %d", errno);
}

- (void)testPtyBackgroundReadStopIsWaitpidVisible {
    XCTAssertEqual(wait_job_control_contract_pty_background_read_stop_is_waitpid_visible(), 0, @"errno %d", errno);
}

- (void)testPtyBackgroundWriteTostopStopIsWaitpidVisible {
    XCTAssertEqual(wait_job_control_contract_pty_background_write_tostop_stop_is_waitpid_visible(), 0, @"errno %d", errno);
}

- (void)testPtyVsuspStopIsWaitpidVisible {
    XCTAssertEqual(wait_job_control_contract_pty_vsusp_stop_is_waitpid_visible(), 0, @"errno %d", errno);
}

- (void)testWaitpidSignalInterruptRecordsRestart {
    XCTAssertEqual(wait_job_control_contract_waitpid_signal_interrupt_records_restart(), 0, @"errno %d", errno);
}

- (void)testCloneThreadIsNotWaitable {
    XCTAssertEqual(wait_job_control_contract_clone_thread_is_not_waitable(), 0, @"errno %d", errno);
}

@end

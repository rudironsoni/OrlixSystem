#import <XCTest/XCTest.h>

#include <errno.h>
#include <string.h>

#include "kernel/init.h"
#include "kernel/signal.h"
#include "kernel/task.h"
#include "PTYJobControlContract.h"

static void reset_pty_job_control_test_kernel_state(void) {
    struct task_struct *child;

    XCTAssertEqual(start_kernel(), 0, @"start_kernel must succeed before PTY job-control tests");
    XCTAssertTrue(kernel_is_booted(), @"kernel must be booted before PTY job-control tests");
    XCTAssertNotEqual(init_task, NULL, @"init_task must exist before PTY job-control tests");

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
        free_task(child);
    }

    set_current(init_task);
}

@interface PTYJobControlTests : XCTestCase
@end

@implementation PTYJobControlTests

- (void)setUp {
    [super setUp];
    reset_pty_job_control_test_kernel_state();
}

- (void)tearDown {
    reset_pty_job_control_test_kernel_state();
    [super tearDown];
}

- (void)testTIOCSPGRPRoundTrip {
    XCTAssertEqual(pty_job_control_contract_tiocspgrp_round_trip(), 0, @"errno %d", errno);
}

- (void)testBackgroundTIOCSPGRPDeliversSIGTTOU {
    XCTAssertEqual(pty_job_control_contract_background_tiocspgrp_delivers_sigttou(), 0, @"errno %d", errno);
}

- (void)testBackgroundReadDeliversSIGTTIN {
    XCTAssertEqual(pty_job_control_contract_background_read_delivers_sigttin(), 0, @"errno %d", errno);
}

- (void)testBackgroundWriteDeliversSIGTTOU {
    XCTAssertEqual(pty_job_control_contract_background_write_delivers_sigttou(), 0, @"errno %d", errno);
}

- (void)testSignalCharsTargetForegroundProcessGroup {
    XCTAssertEqual(pty_job_control_contract_signal_chars_target_foreground_pgrp(), 0, @"errno %d", errno);
}

- (void)testDetachClearsDevTtyPolicy {
    XCTAssertEqual(pty_job_control_contract_detach_clears_dev_tty_policy(), 0, @"errno %d", errno);
}

@end

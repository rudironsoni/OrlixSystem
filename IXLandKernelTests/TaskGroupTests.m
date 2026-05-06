//
// TaskGroupTests.m
// IXLandKernelTests
//
// INTERNAL RUNTIME SEMANTIC TEST
// NOT public wrapper compatibility proof
//
// This file intentionally tests internal IXLandSystem owner semantics
// through internal entry points (getpgrp_impl, setpgid_impl, etc.).
//
// Public drop-in compatibility is deferred to IXLandMLibC/sysroot integration.
//
// Allowed includes:
//   - "kernel/task.h" (private owner header)
//   - "kernel/signal.h" (private owner header)
//   - <errno.h>, <stdint.h>, <stdbool.h>, <string.h> (neutral C headers)
//
// Forbidden includes:
//   - <unistd.h>, <signal.h>, <sys/wait.h> (public POSIX)
//   - <asm-generic/signal.h>, <asm-generic/signal-defs.h> (Linux UAPI in .m)
//   - path traversal into third_party/linux
//   - manual extern declarations for public POSIX names
//   - calling public names like getpgid(), setpgid(), killpg(), waitpid()
//

#import <XCTest/XCTest.h>
#include <errno.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* Internal headers - these are the OWNER entry points we test */
#include "kernel/task.h"
#include "kernel/signal.h"

/* Declare library init function */
extern int library_init(const void *config);
extern int library_is_initialized(void);

@interface TaskGroupTests : XCTestCase
@end

@implementation TaskGroupTests

- (void)setUp {
    [super setUp];
    if (!library_is_initialized()) {
        library_init(NULL);
    }
    /* Clean up any lingering file descriptors using owner close_impl */
    for (int fd = 3; fd < 256; fd++) {
        close_impl(fd);
    }
}

- (void)tearDown {
    /* Clean up any open file descriptors using owner close_impl */
    for (int fd = 3; fd < 256; fd++) {
        close_impl(fd);
    }
    [super tearDown];
}

#pragma mark - A. Initial Task PGID/SID Coherence

- (void)testInitialTaskPgidAndSidCoherence {
    struct task_struct *task = get_current();
    XCTAssertTrue(task != NULL, @"Should have a current task");

    XCTAssertEqual(task->pgid, task->pid, @"Initial task should be its own process group leader");
    XCTAssertEqual(task->sid, task->pid, @"Initial task should be its own session leader");
}

#pragma mark - B. getpgrp_impl Current Group Behavior

- (void)testGetpgrpImplReturnsCurrentTaskPgid {
    struct task_struct *task = get_current();
    XCTAssertTrue(task != NULL, @"Should have a current task");

    int32_t pgid = getpgrp_impl();

    XCTAssertGreaterThan(pgid, 0, @"Process group ID should be positive");
    XCTAssertEqual(pgid, task->pgid, @"getpgrp_impl should return task pgid");
}

#pragma mark - C. getpgid_impl Lookup Behavior

- (void)testGetpgidImplReturnsTargetPgid {
    struct task_struct *task = get_current();
    XCTAssertTrue(task != NULL, @"Should have a current task");

    int32_t pgid = getpgid_impl(task->pid);

    XCTAssertGreaterThan(pgid, 0, @"Process group ID should be positive");
    XCTAssertEqual(pgid, task->pgid, @"getpgid_impl should return task pgid");
}

- (void)testGetpgidImplZeroReturnsCurrentPgid {
    struct task_struct *task = get_current();
    XCTAssertTrue(task != NULL, @"Should have a current task");

    int32_t pgid_zero = getpgid_impl(0);
    int32_t pgid_explicit = getpgid_impl(task->pid);

    XCTAssertEqual(pgid_zero, pgid_explicit, @"getpgid_impl(0) should equal getpgid_impl(pid)");
}

- (void)testGetpgidImplRejectsInvalidPid {
    errno = 0;
    int32_t pgid = getpgid_impl(-9999);

    XCTAssertEqual(pgid, -1, @"Should return -1 for invalid pid");
    XCTAssertEqual(errno, ESRCH, @"errno should be ESRCH");
}

#pragma mark - D. setpgid_impl Validation

- (void)testSetpgidImplRejectsNegativePgid {
    struct task_struct *task = get_current();
    XCTAssertTrue(task != NULL, @"Should have a current task");

    errno = 0;
    int result = setpgid_impl(task->pid, -5);

    XCTAssertEqual(result, -1, @"Should fail with negative pgid");
    XCTAssertEqual(errno, EINVAL, @"errno should be EINVAL");
}

- (void)testSetpgidImplRejectsInvalidPid {
    errno = 0;
    int result = setpgid_impl(-9999, 0);

    XCTAssertEqual(result, -1, @"Should fail");
    XCTAssertEqual(errno, ESRCH, @"errno should be ESRCH");
}

- (void)testSetpgidImplRejectsSessionLeader {
    struct task_struct *task = get_current();
    XCTAssertTrue(task != NULL, @"Should have a current task");

    // If task is session leader, setpgid to different group should fail
    if (task->sid == task->pid) {
        errno = 0;
        int result = setpgid_impl(task->pid, task->pid + 1);

        // Session leader cannot join another process group
        XCTAssertEqual(result, -1, @"Session leader should not be able to change pgid");
        XCTAssertEqual(errno, EPERM, @"errno should be EPERM");
    } else {
        XCTSkip(@"Task is not session leader");
    }
}

- (void)testSetpgidImplCreatesNewGroupWithZero {
    struct task_struct *task = get_current();
    XCTAssertTrue(task != NULL, @"Should have a current task");

    errno = 0;
    int result = setpgid_impl(task->pid, task->pid);

    // May succeed (already in that group) or fail (if session leader constraints)
    if (result == -1) {
        XCTAssertTrue(errno == EPERM, @"Expected EPERM if session leader");
    } else {
        int32_t new_pgid = getpgid_impl(task->pid);
        XCTAssertEqual(new_pgid, task->pid, @"pgid should be task pid");
    }
}

#pragma mark - E. setsid_impl Session Management

- (void)testSetsidImplRejectsProcessGroupLeader {
    struct task_struct *task = get_current();
    XCTAssertTrue(task != NULL, @"Should have a current task");

    // If task is process group leader, setsid should fail with EPERM
    if (task->pgid == task->pid) {
        errno = 0;
        int32_t result = setsid_impl();

        XCTAssertEqual(result, -1, @"setsid should fail for process group leader");
        XCTAssertEqual(errno, EPERM, @"errno should be EPERM");
    } else {
        XCTSkip(@"Task is not process group leader");
    }
}

#pragma mark - F. Signal Mask in Task Context

- (void)testSignalMaskInTaskContext {
    struct task_struct *task = get_current();
    XCTAssertTrue(task != NULL, @"Should have a current task");
    XCTAssertTrue(task->signal != NULL, @"Task should have signal state");

    struct signal_mask_bits mask = {0};
    struct signal_mask_bits oldmask = {0};

    // Block SIGUSR1 (signal 10)
    mask.sig[(10 - 1) >> 6] |= (1ULL << ((10 - 1) & 63));

    errno = 0;
    int result = do_sigprocmask(0, &mask, &oldmask);  // 0 = SIG_BLOCK
    XCTAssertEqual(result, 0, @"SIG_BLOCK should succeed in task context");

    // Verify blocked
    bool is_blocked = signal_is_blocked(task, 10);
    XCTAssertTrue(is_blocked, @"SIGUSR1 should be blocked in task context");

    // Unblock
    result = do_sigprocmask(1, &mask, NULL);  // 1 = SIG_UNBLOCK
    XCTAssertEqual(result, 0, @"SIG_UNBLOCK should succeed");

    is_blocked = signal_is_blocked(task, 10);
    XCTAssertFalse(is_blocked, @"SIGUSR1 should be unblocked");
}

@end

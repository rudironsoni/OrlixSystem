//
// SignalTests.m
// IXLandSystemTests
//
// INTERNAL RUNTIME SEMANTIC TEST
// NOT public wrapper compatibility proof
//
// This file tests internal IXLandSystem signal semantics through
// internal entry points (do_sigprocmask, do_sigaction, do_raise, etc.).
//
// Uses IXLand headers which provide the Linux behavior surface.
//

#import <XCTest/XCTest.h>
#include <errno.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* IXLand internal headers - provide Linux UAPI-compatible constants and behavior */
#include "kernel/signal.h"
#include "kernel/task.h"

extern int signal_syscall_contract_pidfd_send_signal_obeys_linux_targeting_rules(void);

/* Declare library init function */
extern int library_init(const void *config);
extern int library_is_initialized(void);

@interface SignalTests : XCTestCase
@end

@implementation SignalTests

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

#pragma mark - A. Library Initialization

- (void)testLibraryInitialization {
    BOOL isInit = library_is_initialized();
    XCTAssertTrue(isInit, @"Library should be initialized");
}

#pragma mark - B. Signal Mask Internal Operations

- (void)testDoSigprocmaskBasicOperations {
    struct task_struct *task = get_current();
    XCTAssertTrue(task != NULL, @"Should have a current task");
    XCTAssertTrue(task->signal != NULL, @"Task should have signal state");

    struct signal_mask_bits mask = {0};
    struct signal_mask_bits oldmask = {0};
    struct signal_mask_bits queried = {0};

    // Block signal 10 (SIGUSR1)
    mask.sig[(10 - 1) >> 6] |= (1ULL << ((10 - 1) & 63));

    errno = 0;
    int result = do_sigprocmask(0, &mask, &oldmask);  // 0 = SIG_BLOCK
    XCTAssertEqual(result, 0, @"do_sigprocmask SIG_BLOCK should succeed");

    // Query mask
    errno = 0;
    result = do_sigprocmask(0, NULL, &queried);
    XCTAssertEqual(result, 0, @"do_sigprocmask query should succeed");

    // Signal 10 should be blocked
    bool is_blocked = signal_is_blocked(task, 10);
    XCTAssertTrue(is_blocked, @"SIGUSR1 should be blocked");

    // Unblock signal 10
    mask.sig[(10 - 1) >> 6] |= (1ULL << ((10 - 1) & 63));
    errno = 0;
    result = do_sigprocmask(1, &mask, NULL);  // 1 = SIG_UNBLOCK
    XCTAssertEqual(result, 0, @"do_sigprocmask SIG_UNBLOCK should succeed");

    is_blocked = signal_is_blocked(task, 10);
    XCTAssertFalse(is_blocked, @"SIGUSR1 should be unblocked after SIG_UNBLOCK");
}

- (void)testDoSigprocmaskInvalidHow {
    struct signal_mask_bits mask = {0};
    struct signal_mask_bits oldmask = {0};

    errno = 0;
    int result = do_sigprocmask(999, &mask, &oldmask);
    XCTAssertEqual(result, -1, @"do_sigprocmask with invalid 'how' should fail");
    XCTAssertEqual(errno, EINVAL, @"errno should be EINVAL");
}

#pragma mark - C. Signal Generation Internal Operations

- (void)testDoRaiseSignalToSelf {
    struct task_struct *task = get_current();
    XCTAssertTrue(task != NULL, @"Should have a current task");
    XCTAssertTrue(task->signal != NULL, @"Task should have signal state");

    // Block signal 10 (SIGUSR1) first so it becomes pending
    struct signal_mask_bits mask = {0};
    struct signal_mask_bits oldmask = {0};
    mask.sig[(10 - 1) >> 6] |= (1ULL << ((10 - 1) & 63));

    int result = do_sigprocmask(0, &mask, &oldmask);
    XCTAssertEqual(result, 0, @"Block SIGUSR1 should succeed");

    // Clear pending first
    task->thread_pending_signals = 0;
    memset(&task->signal->shared_pending, 0, sizeof(task->signal->shared_pending));

    // Raise signal 10 to self
    errno = 0;
    result = do_raise(10);
    XCTAssertEqual(result, 0, @"do_raise should succeed");

    // Check pending
    struct signal_mask_bits pending = {0};
    result = do_sigpending(&pending);
    XCTAssertEqual(result, 0, @"do_sigpending should succeed");

    bool is_pending = (pending.sig[(10 - 1) >> 6] & (1ULL << ((10 - 1) & 63))) != 0;
    XCTAssertTrue(is_pending, @"SIGUSR1 should be pending after do_raise while blocked");

    // Restore mask
    do_sigprocmask(2, &oldmask, NULL);  // 2 = SIG_SETMASK
}

- (void)testDoKillSignalToCurrentTask {
    struct task_struct *task = get_current();
    XCTAssertTrue(task != NULL, @"Should have a current task");
    XCTAssertTrue(task->signal != NULL, @"Task should have signal state");

    // Block signal 10 first
    struct signal_mask_bits mask = {0};
    struct signal_mask_bits oldmask = {0};
    mask.sig[(10 - 1) >> 6] |= (1ULL << ((10 - 1) & 63));

    int result = do_sigprocmask(0, &mask, &oldmask);
    XCTAssertEqual(result, 0, @"Block SIGUSR1 should succeed");

    // Clear pending first
    task->thread_pending_signals = 0;
    memset(&task->signal->shared_pending, 0, sizeof(task->signal->shared_pending));

    // Send signal 10 to current task via do_kill
    errno = 0;
    result = do_kill(task->pid, 10);
    XCTAssertEqual(result, 0, @"do_kill to current task should succeed");

    // Check pending
    struct signal_mask_bits pending = {0};
    result = do_sigpending(&pending);
    XCTAssertEqual(result, 0, @"do_sigpending should succeed");

    bool is_pending = (pending.sig[(10 - 1) >> 6] & (1ULL << ((10 - 1) & 63))) != 0;
    XCTAssertTrue(is_pending, @"SIGUSR1 should be pending after do_kill while blocked");

    // Restore mask
    do_sigprocmask(2, &oldmask, NULL);
}

#pragma mark - D. Signal Action Internal Operations

- (void)testDoSigactionBasicOperations {
    struct task_struct *task = get_current();
    XCTAssertTrue(task != NULL, @"Should have a current task");
    XCTAssertTrue(task->signal != NULL, @"Task should have signal state");

    struct signal_action_slot new_act = {0};
    struct signal_action_slot old_act = {0};

    // Set signal 10 to default handler
    new_act.handler = NULL;  // SIG_DFL = NULL
    memset(&new_act.mask, 0, sizeof(struct signal_mask_bits));

    errno = 0;
    int result = do_sigaction(10, &new_act, &old_act);
    XCTAssertEqual(result, 0, @"do_sigaction should succeed");

    // Query current action
    result = do_sigaction(10, NULL, &old_act);
    XCTAssertEqual(result, 0, @"do_sigaction query should succeed");

    // Handler should be SIG_DFL (NULL)
    XCTAssertTrue(old_act.handler == NULL, @"Handler should be SIG_DFL");
}

- (void)testDoSigactionIgnoresUnblockableSignals {
    struct signal_action_slot new_act = {0};
    struct signal_action_slot old_act = {0};

    new_act.handler = NULL;

    // Try to change SIGKILL (9) - should fail
    errno = 0;
    int result = do_sigaction(9, &new_act, &old_act);
    XCTAssertEqual(result, -1, @"do_sigaction should fail for SIGKILL");
    XCTAssertEqual(errno, EINVAL, @"errno should be EINVAL");

    // Try to change SIGSTOP (19) - should fail
    errno = 0;
    result = do_sigaction(19, &new_act, &old_act);
    XCTAssertEqual(result, -1, @"do_sigaction should fail for SIGSTOP");
    XCTAssertEqual(errno, EINVAL, @"errno should be EINVAL");
}

#pragma mark - E. Signal Pending and Query Operations

- (void)testDoSigpendingBasicOperations {
    struct task_struct *task = get_current();
    XCTAssertTrue(task != NULL, @"Should have a current task");
    XCTAssertTrue(task->signal != NULL, @"Task should have signal state");

    // Block signal 12 (SIGUSR2) first
    struct signal_mask_bits mask = {0};
    struct signal_mask_bits oldmask = {0};
    mask.sig[(12 - 1) >> 6] |= (1ULL << ((12 - 1) & 63));

    int result = do_sigprocmask(0, &mask, &oldmask);
    XCTAssertEqual(result, 0, @"Block SIGUSR2 should succeed");

    // Clear pending
    task->thread_pending_signals = 0;
    memset(&task->signal->shared_pending, 0, sizeof(task->signal->shared_pending));

    // Check no pending
    struct signal_mask_bits pending = {0};
    result = do_sigpending(&pending);
    XCTAssertEqual(result, 0, @"do_sigpending should succeed");

    bool has_pending = (pending.sig[(12 - 1) >> 6] & (1ULL << ((12 - 1) & 63))) != 0;
    XCTAssertFalse(has_pending, @"No signal should be pending initially");

    // Generate signal 12
    result = do_raise(12);
    XCTAssertEqual(result, 0, @"do_raise SIGUSR2 should succeed");

    // Check pending again
    result = do_sigpending(&pending);
    XCTAssertEqual(result, 0, @"do_sigpending should succeed");

    has_pending = (pending.sig[(12 - 1) >> 6] & (1ULL << ((12 - 1) & 63))) != 0;
    XCTAssertTrue(has_pending, @"SIGUSR2 should be pending");

    // Restore mask
    do_sigprocmask(2, &oldmask, NULL);
}

#pragma mark - F. Signal Set Operations

- (void)testSignalSetOperations {
    struct signal_mask_bits set = {0};
    int signum;
    int idx;
    int bit;
    bool is_member;

    // Initially empty - check signal 10
    signum = 10;
    idx = (signum - 1) >> 6;
    bit = (signum - 1) & 63;
    is_member = (set.sig[idx] & (1ULL << bit)) != 0;
    XCTAssertFalse(is_member, @"SIGUSR1 should not be in empty set");

    // Add signal 10
    set.sig[idx] |= (1ULL << bit);
    is_member = (set.sig[idx] & (1ULL << bit)) != 0;
    XCTAssertTrue(is_member, @"SIGUSR1 should be in set after add");

    // Add signal 12
    signum = 12;
    idx = (signum - 1) >> 6;
    bit = (signum - 1) & 63;
    set.sig[idx] |= (1ULL << bit);
    is_member = (set.sig[idx] & (1ULL << bit)) != 0;
    XCTAssertTrue(is_member, @"SIGUSR2 should be in set after add");

    // Del signal 10
    signum = 10;
    idx = (signum - 1) >> 6;
    bit = (signum - 1) & 63;
    set.sig[idx] &= ~(1ULL << bit);
    is_member = (set.sig[idx] & (1ULL << bit)) != 0;
    XCTAssertFalse(is_member, @"SIGUSR1 should not be in set after del");

    // Signal 12 should still be there
    signum = 12;
    idx = (signum - 1) >> 6;
    bit = (signum - 1) & 63;
    is_member = (set.sig[idx] & (1ULL << bit)) != 0;
    XCTAssertTrue(is_member, @"SIGUSR2 should still be in set");

    // Fill set (set all bits)
    for (int i = 0; i < KERNEL_SIG_NUM_WORDS; i++) {
        set.sig[i] = ~0ULL;
    }
    signum = 17;
    idx = (signum - 1) >> 6;
    bit = (signum - 1) & 63;
    is_member = (set.sig[idx] & (1ULL << bit)) != 0;
    XCTAssertTrue(is_member, @"SIGCHLD should be in filled set");

    // Empty set
    memset(&set, 0, sizeof(set));
    signum = 10;
    idx = (signum - 1) >> 6;
    bit = (signum - 1) & 63;
    is_member = (set.sig[idx] & (1ULL << bit)) != 0;
    XCTAssertFalse(is_member, @"SIGUSR1 should not be in empty set");
}

#pragma mark - G. Signal Blocked Check

- (void)testSignalIsBlocked {
    struct task_struct *task = get_current();
    XCTAssertTrue(task != NULL, @"Should have a current task");

    struct signal_mask_bits mask = {0};
    struct signal_mask_bits oldmask = {0};

    // Initially signal 17 (SIGCHLD) should not be blocked
    bool is_blocked = signal_is_blocked(task, 17);
    XCTAssertFalse(is_blocked, @"SIGCHLD should not be blocked initially");

    // Block signal 17
    mask.sig[(17 - 1) >> 6] |= (1ULL << ((17 - 1) & 63));
    int result = do_sigprocmask(0, &mask, &oldmask);
    XCTAssertEqual(result, 0, @"Block SIGCHLD should succeed");

    // Now signal 17 should be blocked
    is_blocked = signal_is_blocked(task, 17);
    XCTAssertTrue(is_blocked, @"SIGCHLD should be blocked");

    // Signal 10 should still not be blocked
    is_blocked = signal_is_blocked(task, 10);
    XCTAssertFalse(is_blocked, @"SIGUSR1 should not be blocked");

    // Restore
    do_sigprocmask(2, &oldmask, NULL);
}

#pragma mark - H. pidfd Signal Syscall Contract

- (void)testPidfdSendSignalUsesProcessDirectedLinuxSemantics {
    XCTAssertEqual(signal_syscall_contract_pidfd_send_signal_obeys_linux_targeting_rules(), 0,
                   @"errno %d", errno);
}

@end

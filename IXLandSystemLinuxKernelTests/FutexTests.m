#import <XCTest/XCTest.h>

#include <errno.h>

#include "FutexContract.h"

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

- (void)testClearChildTidIsPerThreadNotMmShared {
    XCTAssertEqual(futex_contract_clear_child_tid_is_per_thread_not_mm_shared(), 0, @"errno %d", errno);
}

@end

#import <XCTest/XCTest.h>

#include "fs/fdtable.h"
#include "kernel/init.h"

extern int fcntl_contract_dup_returns_lowest_available_fd(void);
extern int fcntl_contract_dup_shares_offset(void);
extern int fcntl_contract_dup_does_not_copy_cloexec(void);
extern int fcntl_contract_dup2_same_fd_returns_fd_when_valid(void);
extern int fcntl_contract_dup2_same_fd_invalid_returns_badf(void);
extern int fcntl_contract_dup2_replaces_open_fd(void);
extern int fcntl_contract_dup2_does_not_copy_cloexec(void);
extern int fcntl_contract_dup3_same_fd_returns_inval(void);
extern int fcntl_contract_dup3_cloexec_sets_close_on_exec(void);
extern int fcntl_contract_f_dupfd_honors_minimum_fd(void);
extern int fcntl_contract_f_dupfd_rejects_negative_minimum(void);
extern int fcntl_contract_f_dupfd_rejects_out_of_range_minimum(void);
extern int fcntl_contract_f_dupfd_cloexec_sets_close_on_exec(void);
extern int fcntl_contract_setfd_cloexec_is_per_descriptor(void);
extern int fcntl_contract_getfl_does_not_report_fd_cloexec(void);
extern int fcntl_contract_setfl_does_not_mutate_access_mode(void);
extern int fcntl_contract_setfl_does_not_set_close_on_exec(void);
extern int fcntl_contract_setfl_append_affects_duplicated_fd_status(void);
extern int fcntl_contract_proc_self_fdinfo_reflects_close_on_exec_per_descriptor(void);
extern int fcntl_contract_proc_self_fdinfo_reflects_nonblock_after_setfl(void);
extern int fcntl_contract_child_dup2_does_not_replace_parent_descriptor(void);
extern int fcntl_contract_pidfd_getfd_duplicates_target_descriptor(void);
extern int fcntl_contract_pidfd_getfd_rejects_permission_mismatch(void);
extern int fcntl_contract_pidfd_getfd_allows_ptrace_eligible_sibling(void);
extern int fcntl_contract_pidfd_getfd_rejects_bad_targets(void);
extern void fcntl_contract_reset_pidfd_test_state(void);

@interface FcntlTests : XCTestCase
@end

@implementation FcntlTests

- (void)setUp {
    [super setUp];
    XCTAssertEqual(start_kernel(), 0, @"start_kernel must succeed before LinuxKernel fcntl tests");
    XCTAssertTrue(kernel_is_booted(), @"kernel must be booted before LinuxKernel fcntl tests");
    fcntl_contract_reset_pidfd_test_state();
    for (int fd = 3; fd < 256; fd++) {
        close_impl(fd);
    }
}

- (void)tearDown {
    for (int fd = 3; fd < 256; fd++) {
        close_impl(fd);
    }
    fcntl_contract_reset_pidfd_test_state();
    [super tearDown];
}

- (void)testDupReturnsLowestAvailableFd {
    XCTAssertEqual(fcntl_contract_dup_returns_lowest_available_fd(), 0);
}

- (void)testDupSharesFileOffset {
    XCTAssertEqual(fcntl_contract_dup_shares_offset(), 0);
}

- (void)testDupDoesNotCopyCloseOnExec {
    XCTAssertEqual(fcntl_contract_dup_does_not_copy_cloexec(), 0);
}

- (void)testDup2SameFdReturnsFdWhenValid {
    XCTAssertEqual(fcntl_contract_dup2_same_fd_returns_fd_when_valid(), 0);
}

- (void)testDup2SameFdInvalidReturnsBadf {
    XCTAssertEqual(fcntl_contract_dup2_same_fd_invalid_returns_badf(), 0);
}

- (void)testDup2ReplacesOpenFd {
    XCTAssertEqual(fcntl_contract_dup2_replaces_open_fd(), 0);
}

- (void)testDup2DoesNotCopyCloseOnExec {
    XCTAssertEqual(fcntl_contract_dup2_does_not_copy_cloexec(), 0);
}

- (void)testDup3SameFdReturnsInval {
    XCTAssertEqual(fcntl_contract_dup3_same_fd_returns_inval(), 0);
}

- (void)testDup3CloexecSetsCloseOnExec {
    XCTAssertEqual(fcntl_contract_dup3_cloexec_sets_close_on_exec(), 0);
}

- (void)testFcntlDupfdHonorsMinimumFd {
    XCTAssertEqual(fcntl_contract_f_dupfd_honors_minimum_fd(), 0);
}

- (void)testFcntlDupfdRejectsNegativeMinimum {
    XCTAssertEqual(fcntl_contract_f_dupfd_rejects_negative_minimum(), 0);
}

- (void)testFcntlDupfdRejectsOutOfRangeMinimum {
    XCTAssertEqual(fcntl_contract_f_dupfd_rejects_out_of_range_minimum(), 0);
}

- (void)testFcntlDupfdCloexecSetsCloseOnExec {
    XCTAssertEqual(fcntl_contract_f_dupfd_cloexec_sets_close_on_exec(), 0);
}

- (void)testFcntlSetfdCloexecIsPerDescriptor {
    XCTAssertEqual(fcntl_contract_setfd_cloexec_is_per_descriptor(), 0);
}

- (void)testFcntlGetflDoesNotReportFdCloexec {
    XCTAssertEqual(fcntl_contract_getfl_does_not_report_fd_cloexec(), 0);
}

- (void)testFcntlSetflDoesNotMutateAccessMode {
    XCTAssertEqual(fcntl_contract_setfl_does_not_mutate_access_mode(), 0);
}

- (void)testFcntlSetflDoesNotSetCloseOnExec {
    XCTAssertEqual(fcntl_contract_setfl_does_not_set_close_on_exec(), 0);
}

- (void)testFcntlSetflAppendAffectsDuplicatedFdStatus {
    XCTAssertEqual(fcntl_contract_setfl_append_affects_duplicated_fd_status(), 0);
}

- (void)testProcSelfFdinfoReflectsCloseOnExecPerDescriptor {
    XCTAssertEqual(fcntl_contract_proc_self_fdinfo_reflects_close_on_exec_per_descriptor(), 0, @"errno %d", errno);
}

- (void)testProcSelfFdinfoReflectsNonblockAfterSetfl {
    XCTAssertEqual(fcntl_contract_proc_self_fdinfo_reflects_nonblock_after_setfl(), 0, @"errno %d", errno);
}

- (void)testChildDup2DoesNotReplaceParentDescriptor {
    XCTAssertEqual(fcntl_contract_child_dup2_does_not_replace_parent_descriptor(), 0, @"errno %d", errno);
}

- (void)testPidfdGetfdDuplicatesTargetDescriptor {
    XCTAssertEqual(fcntl_contract_pidfd_getfd_duplicates_target_descriptor(), 0, @"errno %d", errno);
}

- (void)testPidfdGetfdRejectsPermissionMismatch {
    XCTAssertEqual(fcntl_contract_pidfd_getfd_rejects_permission_mismatch(), 0, @"errno %d", errno);
}

- (void)testPidfdGetfdAllowsPtraceEligibleSibling {
    XCTAssertEqual(fcntl_contract_pidfd_getfd_allows_ptrace_eligible_sibling(), 0, @"errno %d", errno);
}

- (void)testPidfdGetfdRejectsBadTargets {
    XCTAssertEqual(fcntl_contract_pidfd_getfd_rejects_bad_targets(), 0, @"errno %d", errno);
}

@end

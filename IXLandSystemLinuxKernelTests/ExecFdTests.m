#import <XCTest/XCTest.h>

#include "fs/fdtable.h"
#include "kernel/init.h"

extern int exec_fd_contract_close_on_exec_closes_only_cloexec_descriptor(void);
extern int exec_fd_contract_close_on_exec_preserves_descriptor_without_cloexec(void);
extern int exec_fd_contract_close_on_exec_does_not_close_shared_description_still_referenced(void);
extern int exec_fd_contract_close_on_exec_closes_duplicated_descriptor_with_own_cloexec_flag(void);
extern int exec_fd_contract_close_on_exec_preserves_source_descriptor_when_duplicate_is_cloexec(void);
extern int exec_fd_contract_close_on_exec_removes_closed_fd_from_proc_self_fd(void);
extern int exec_fd_contract_close_on_exec_removes_closed_fd_from_proc_self_fdinfo(void);
extern int exec_fd_contract_close_on_exec_preserves_non_cloexec_fd_in_proc_self_fd(void);
extern int exec_fd_contract_close_on_exec_works_for_synthetic_dev_fd(void);
extern int exec_fd_contract_close_on_exec_works_for_synthetic_proc_directory_fd(void);
extern int exec_fd_contract_close_on_exec_works_for_synthetic_proc_file_fd(void);
extern int exec_fd_contract_close_on_exec_is_idempotent_when_no_cloexec_fds_remain(void);
extern int exec_fd_contract_close_on_exec_keeps_fd_allocation_deterministic(void);
extern int exec_fd_contract_close_on_exec_does_not_mutate_status_flags_on_survivors(void);

@interface ExecFdTests : XCTestCase
@end

@implementation ExecFdTests

- (void)setUp {
    [super setUp];
    XCTAssertEqual(start_kernel(), 0, @"start_kernel must succeed before LinuxKernel exec-fd tests");
    XCTAssertTrue(kernel_is_booted(), @"kernel must be booted before LinuxKernel exec-fd tests");
    for (int fd = 3; fd < 256; fd++) {
        close_impl(fd);
    }
}

- (void)tearDown {
    for (int fd = 3; fd < 256; fd++) {
        close_impl(fd);
    }
    [super tearDown];
}

- (void)testCloseOnExecClosesOnlyCloexecDescriptor {
    XCTAssertEqual(exec_fd_contract_close_on_exec_closes_only_cloexec_descriptor(), 0);
}

- (void)testCloseOnExecPreservesDescriptorWithoutCloexec {
    XCTAssertEqual(exec_fd_contract_close_on_exec_preserves_descriptor_without_cloexec(), 0);
}

- (void)testCloseOnExecDoesNotCloseSharedDescriptionStillReferenced {
    XCTAssertEqual(exec_fd_contract_close_on_exec_does_not_close_shared_description_still_referenced(), 0);
}

- (void)testCloseOnExecClosesDuplicatedDescriptorWithOwnCloexecFlag {
    XCTAssertEqual(exec_fd_contract_close_on_exec_closes_duplicated_descriptor_with_own_cloexec_flag(), 0);
}

- (void)testCloseOnExecPreservesSourceDescriptorWhenDuplicateIsCloexec {
    XCTAssertEqual(exec_fd_contract_close_on_exec_preserves_source_descriptor_when_duplicate_is_cloexec(), 0);
}

- (void)testCloseOnExecRemovesClosedFdFromProcSelfFd {
    XCTAssertEqual(exec_fd_contract_close_on_exec_removes_closed_fd_from_proc_self_fd(), 0);
}

- (void)testCloseOnExecRemovesClosedFdFromProcSelfFdinfo {
    XCTAssertEqual(exec_fd_contract_close_on_exec_removes_closed_fd_from_proc_self_fdinfo(), 0);
}

- (void)testCloseOnExecPreservesNonCloexecFdInProcSelfFd {
    XCTAssertEqual(exec_fd_contract_close_on_exec_preserves_non_cloexec_fd_in_proc_self_fd(), 0);
}

- (void)testCloseOnExecWorksForSyntheticDevFd {
    XCTAssertEqual(exec_fd_contract_close_on_exec_works_for_synthetic_dev_fd(), 0);
}

- (void)testCloseOnExecWorksForSyntheticProcDirectoryFd {
    XCTAssertEqual(exec_fd_contract_close_on_exec_works_for_synthetic_proc_directory_fd(), 0, @"errno %d", errno);
}

- (void)testCloseOnExecWorksForSyntheticProcFileFd {
    XCTAssertEqual(exec_fd_contract_close_on_exec_works_for_synthetic_proc_file_fd(), 0);
}

- (void)testCloseOnExecIsIdempotentWhenNoCloexecFdsRemain {
    XCTAssertEqual(exec_fd_contract_close_on_exec_is_idempotent_when_no_cloexec_fds_remain(), 0);
}

- (void)testCloseOnExecKeepsFdAllocationDeterministic {
    XCTAssertEqual(exec_fd_contract_close_on_exec_keeps_fd_allocation_deterministic(), 0);
}

- (void)testCloseOnExecDoesNotMutateStatusFlagsOnSurvivors {
    XCTAssertEqual(exec_fd_contract_close_on_exec_does_not_mutate_status_flags_on_survivors(), 0, @"errno %d", errno);
}

@end

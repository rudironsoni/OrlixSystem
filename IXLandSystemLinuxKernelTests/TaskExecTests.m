#import <XCTest/XCTest.h>

#include <errno.h>

#include "fs/fdtable.h"
#include "kernel/init.h"

extern int task_exec_contract_rejects_missing_current_task(void);
extern int task_exec_contract_rejects_null_path(void);
extern int task_exec_contract_rejects_empty_path(void);
extern int task_exec_contract_rejects_too_long_path(void);
extern int task_exec_contract_updates_task_state_and_closes_cloexec_fds(void);
extern int task_exec_contract_uses_basename_of_path_when_argv0_is_empty(void);
extern int task_exec_contract_truncates_comm_to_task_comm_len_minus_one(void);
extern int task_exec_contract_preserves_task_identity_and_non_exec_state(void);
extern int task_exec_contract_setuid_mode_updates_virtual_effective_uid(void);
extern int task_exec_contract_setgid_mode_updates_virtual_effective_gid(void);
extern int task_exec_contract_no_new_privs_blocks_setuid_exec_gain(void);
extern int task_exec_contract_no_new_privs_blocks_setgid_exec_gain(void);
extern int task_exec_contract_no_new_privs_is_irreversible(void);
extern int task_exec_contract_keepcaps_preserves_permitted_caps_after_setuid(void);
extern int task_exec_contract_securebits_keepcaps_lock_is_enforced(void);
extern int task_exec_contract_ambient_capability_survives_plain_exec(void);
extern int task_exec_contract_nosuid_mount_blocks_setuid_exec_gain(void);
extern int task_exec_contract_ambient_raise_requires_inheritable_cap(void);
extern int task_exec_contract_securebits_block_ambient_raise(void);

@interface TaskExecTests : XCTestCase
@end

@implementation TaskExecTests

- (void)setUp {
    [super setUp];
    XCTAssertEqual(start_kernel(), 0, @"start_kernel must succeed before LinuxKernel task-exec tests");
    XCTAssertTrue(kernel_is_booted(), @"kernel must be booted before LinuxKernel task-exec tests");
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

- (void)testRejectsMissingCurrentTask {
    XCTAssertEqual(task_exec_contract_rejects_missing_current_task(), 0);
}

- (void)testRejectsNullPath {
    XCTAssertEqual(task_exec_contract_rejects_null_path(), 0);
}

- (void)testRejectsEmptyPath {
    XCTAssertEqual(task_exec_contract_rejects_empty_path(), 0);
}

- (void)testRejectsTooLongPath {
    XCTAssertEqual(task_exec_contract_rejects_too_long_path(), 0);
}

- (void)testUpdatesTaskStateAndClosesCloexecFds {
    XCTAssertEqual(task_exec_contract_updates_task_state_and_closes_cloexec_fds(), 0, @"errno %d", errno);
}

- (void)testUsesBasenameOfPathWhenArgv0IsEmpty {
    XCTAssertEqual(task_exec_contract_uses_basename_of_path_when_argv0_is_empty(), 0);
}

- (void)testTruncatesCommToTaskCommLenMinusOne {
    XCTAssertEqual(task_exec_contract_truncates_comm_to_task_comm_len_minus_one(), 0);
}

- (void)testPreservesTaskIdentityAndNonExecState {
    XCTAssertEqual(task_exec_contract_preserves_task_identity_and_non_exec_state(), 0);
}

- (void)testSetuidModeUpdatesVirtualEffectiveUid {
    XCTAssertEqual(task_exec_contract_setuid_mode_updates_virtual_effective_uid(), 0, @"errno %d", errno);
}

- (void)testSetgidModeUpdatesVirtualEffectiveGid {
    XCTAssertEqual(task_exec_contract_setgid_mode_updates_virtual_effective_gid(), 0, @"errno %d", errno);
}

- (void)testNoNewPrivsBlocksSetuidExecGain {
    XCTAssertEqual(task_exec_contract_no_new_privs_blocks_setuid_exec_gain(), 0, @"errno %d", errno);
}

- (void)testNoNewPrivsBlocksSetgidExecGain {
    XCTAssertEqual(task_exec_contract_no_new_privs_blocks_setgid_exec_gain(), 0, @"errno %d", errno);
}

- (void)testNoNewPrivsIsIrreversible {
    XCTAssertEqual(task_exec_contract_no_new_privs_is_irreversible(), 0, @"errno %d", errno);
}

- (void)testKeepcapsPreservesPermittedCapsAfterSetuid {
    XCTAssertEqual(task_exec_contract_keepcaps_preserves_permitted_caps_after_setuid(), 0, @"errno %d", errno);
}

- (void)testSecurebitsKeepcapsLockIsEnforced {
    XCTAssertEqual(task_exec_contract_securebits_keepcaps_lock_is_enforced(), 0, @"errno %d", errno);
}

- (void)testAmbientCapabilitySurvivesPlainExec {
    XCTAssertEqual(task_exec_contract_ambient_capability_survives_plain_exec(), 0, @"errno %d", errno);
}

- (void)testNosuidMountBlocksSetuidExecGain {
    XCTAssertEqual(task_exec_contract_nosuid_mount_blocks_setuid_exec_gain(), 0, @"errno %d", errno);
}

- (void)testAmbientRaiseRequiresInheritableCap {
    XCTAssertEqual(task_exec_contract_ambient_raise_requires_inheritable_cap(), 0, @"errno %d", errno);
}

- (void)testSecurebitsBlockAmbientRaise {
    XCTAssertEqual(task_exec_contract_securebits_block_ambient_raise(), 0, @"errno %d", errno);
}

@end

#import <XCTest/XCTest.h>

#include <errno.h>

#include "kernel/init.h"
#include "PTYSessionContract.h"

@interface PTYSessionTests : XCTestCase
@end

@implementation PTYSessionTests

- (void)setUp {
    [super setUp];
    XCTAssertEqual(start_kernel(), 0, @"start_kernel must succeed before PTY session tests");
    XCTAssertTrue(kernel_is_booted(), @"kernel must be booted before PTY session tests");
}

- (void)testBootInitTaskHasLinuxSessionIdentity {
    XCTAssertEqual(pty_session_contract_boot_init_task_has_linux_session_identity(), 0, @"errno %d", errno);
}

- (void)testDevTtyFailsWithoutControllingTerminal {
    XCTAssertEqual(pty_session_contract_dev_tty_fails_without_controlling_terminal(), 0, @"errno %d", errno);
}

- (void)testPtyPairAllocatesMasterAndSlaveDescriptors {
    XCTAssertEqual(pty_session_contract_pty_pair_allocates_master_and_slave_descriptors(), 0, @"errno %d", errno);
}

- (void)testPtyDescriptorsAppearInProcSelfFd {
    XCTAssertEqual(pty_session_contract_pty_descriptors_appear_in_proc_self_fd(), 0, @"errno %d", errno);
}

- (void)testPtyFstatReportsCharacterDevice {
    XCTAssertEqual(pty_session_contract_pty_fstat_reports_character_device(), 0, @"errno %d", errno);
}

- (void)testPtyWriteMasterReadSlave {
    XCTAssertEqual(pty_session_contract_pty_write_master_read_slave(), 0, @"errno %d", errno);
}

- (void)testPtyWriteSlaveReadMaster {
    XCTAssertEqual(pty_session_contract_pty_write_slave_read_master(), 0, @"errno %d", errno);
}

- (void)testPtyNonblockingReadWithoutDataReturnsAgain {
    XCTAssertEqual(pty_session_contract_pty_nonblocking_read_without_data_returns_again(), 0, @"errno %d", errno);
}

- (void)testPtyCloseOnExecClosesOnlyFlaggedPtyDescriptor {
    XCTAssertEqual(pty_session_contract_close_on_exec_closes_only_flagged_pty_descriptor(), 0, @"errno %d", errno);
}

- (void)testControllingTtyAttachMakesDevTtyUsable {
    XCTAssertEqual(pty_session_contract_controlling_tty_attach_makes_dev_tty_usable(), 0, @"errno %d", errno);
}

- (void)testControllingTtySurvivesDupOfSlaveDescriptor {
    XCTAssertEqual(pty_session_contract_controlling_tty_survives_dup_of_slave_descriptor(), 0, @"errno %d", errno);
}

- (void)testControllingTtyClearsOrFailsPredictablyAfterClosePolicy {
    XCTAssertEqual(pty_session_contract_controlling_tty_clears_or_fails_predictably_after_close_policy(), 0, @"errno %d", errno);
}

- (void)testPtyFdinfoReflectsFlagsAndPaths {
    XCTAssertEqual(pty_session_contract_pty_fdinfo_reflects_flags_and_paths(), 0, @"errno %d", errno);
}

- (void)testSessionLeaderExitHangsUpForegroundPgrp {
    XCTAssertEqual(pty_session_contract_session_leader_exit_hangs_up_foreground_pgrp(), 0, @"errno %d", errno);
}

- (void)testSessionLeaderTiocnottyHangsUpForegroundPgrp {
    XCTAssertEqual(pty_session_contract_session_leader_tiocnotty_hangs_up_foreground_pgrp(), 0, @"errno %d", errno);
}

@end

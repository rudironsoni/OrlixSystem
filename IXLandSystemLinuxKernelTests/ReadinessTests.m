#import <XCTest/XCTest.h>

#include <errno.h>

#include "fs/fdtable.h"
#include "kernel/init.h"
#include "IXLandSystemLinuxKernelTests/ReadinessContract.h"

@interface ReadinessTests : XCTestCase
@end

@implementation ReadinessTests

- (void)setUp {
    [super setUp];
    XCTAssertEqual(start_kernel(), 0);
    for (int fd = 3; fd < NR_OPEN_DEFAULT; fd++) {
        if (fdtable_is_used_impl(fd)) {
            close_impl(fd);
        }
    }
}

- (void)tearDown {
    for (int fd = 3; fd < NR_OPEN_DEFAULT; fd++) {
        if (fdtable_is_used_impl(fd)) {
            close_impl(fd);
        }
    }
    [super tearDown];
}

- (void)testPollPipeBlocksUntilWriterWrites { XCTAssertEqual(readiness_contract_poll_pipe_blocks_until_writer_writes(), 0, @"errno %d", errno); }
- (void)testPollPipeTimeoutReturnsZero { XCTAssertEqual(readiness_contract_poll_pipe_timeout_returns_zero(), 0, @"errno %d", errno); }
- (void)testPollPipeSignalInterruptReturnsIntr { XCTAssertEqual(readiness_contract_poll_pipe_signal_interrupt_returns_intr(), 0, @"errno %d", errno); }
- (void)testPollMultipleFdsReturnsFirstReadyPipe { XCTAssertEqual(readiness_contract_poll_multiple_fds_returns_first_ready_pipe(), 0, @"errno %d", errno); }
- (void)testPollMultipleFdsWakesWhenSecondPipeBecomesReady { XCTAssertEqual(readiness_contract_poll_multiple_fds_wakes_when_second_pipe_becomes_ready(), 0, @"errno %d", errno); }
- (void)testPollPipeHupAfterWriterClose { XCTAssertEqual(readiness_contract_poll_pipe_hup_after_writer_close(), 0, @"errno %d", errno); }
- (void)testPollPipeWriteEndErrAfterReaderClose { XCTAssertEqual(readiness_contract_poll_pipe_write_end_err_after_reader_close(), 0, @"errno %d", errno); }
- (void)testPollSocketpairBlocksUntilPeerWrites { XCTAssertEqual(readiness_contract_poll_socketpair_blocks_until_peer_writes(), 0, @"errno %d", errno); }
- (void)testPollSocketpairHupAfterPeerClose { XCTAssertEqual(readiness_contract_poll_socketpair_hup_after_peer_close(), 0, @"errno %d", errno); }
- (void)testEventfd2CounterReadWriteAndPoll { XCTAssertEqual(readiness_contract_eventfd2_counter_read_write_and_poll(), 0, @"errno %d", errno); }
- (void)testTimerfdRelativeExpirationReadAndPoll { XCTAssertEqual(readiness_contract_timerfd_relative_expiration_read_and_poll(), 0, @"errno %d", errno); }
- (void)testPollPtyMasterBlocksUntilSlaveWrites { XCTAssertEqual(readiness_contract_poll_pty_master_blocks_until_slave_writes(), 0, @"errno %d", errno); }
- (void)testPollPtySlaveBlocksUntilMasterWrites { XCTAssertEqual(readiness_contract_poll_pty_slave_blocks_until_master_writes(), 0, @"errno %d", errno); }
- (void)testPollPtyHupAfterPeerClose { XCTAssertEqual(readiness_contract_poll_pty_hup_after_peer_close(), 0, @"errno %d", errno); }
- (void)testSelectPipeReadBlocksUntilWriterWrites { XCTAssertEqual(readiness_contract_select_pipe_read_blocks_until_writer_writes(), 0, @"errno %d", errno); }
- (void)testSelectPipeWriteReportsWritable { XCTAssertEqual(readiness_contract_select_pipe_write_reports_writable(), 0, @"errno %d", errno); }
- (void)testSelectTimeoutReturnsZero { XCTAssertEqual(readiness_contract_select_timeout_returns_zero(), 0, @"errno %d", errno); }
- (void)testSelectSignalInterruptReturnsIntr { XCTAssertEqual(readiness_contract_select_signal_interrupt_returns_intr(), 0, @"errno %d", errno); }
- (void)testSelectRestartSyscallReentersReadinessWait { XCTAssertEqual(readiness_contract_select_restart_syscall_reenters_readiness_wait(), 0, @"errno %d", errno); }
- (void)testSelectPtyReadWakesOnPeerWrite { XCTAssertEqual(readiness_contract_select_pty_read_wakes_on_peer_write(), 0, @"errno %d", errno); }
- (void)testPselect6PipeUsesSharedReadinessEngine { XCTAssertEqual(readiness_contract_pselect6_pipe_uses_shared_readiness_engine(), 0, @"errno %d", errno); }
- (void)testPselect6MaskBlocksSignalUntilPipeReadyAndRestores { XCTAssertEqual(readiness_contract_pselect6_mask_blocks_signal_until_pipe_ready_and_restores(), 0, @"errno %d", errno); }
- (void)testProcAndDevFdsReportReadiness { XCTAssertEqual(readiness_contract_proc_and_dev_fds_report_readiness(), 0, @"errno %d", errno); }
- (void)testSyntheticDirsAndDevZeroReportReadiness { XCTAssertEqual(readiness_contract_synthetic_dirs_and_dev_zero_report_readiness(), 0, @"errno %d", errno); }

@end

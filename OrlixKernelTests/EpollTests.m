#import <XCTest/XCTest.h>

#include <errno.h>

#include "kernel/init.h"
#include "EpollContract.h"

extern int close_impl(int fd);

@interface EpollTests : XCTestCase
@end

@implementation EpollTests

- (void)setUp {
    [super setUp];
    XCTAssertEqual(start_kernel(), 0);
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

- (void)testEpollCreateReturnsFd { XCTAssertEqual(epoll_contract_create_returns_fd(), 0, @"errno %d", errno); }
- (void)testEpollCreate1CloexecSetsFdCloexec { XCTAssertEqual(epoll_contract_create1_cloexec_sets_fd_cloexec(), 0, @"errno %d", errno); }
- (void)testEpollCtlAddPipeReadEnd { XCTAssertEqual(epoll_contract_ctl_add_pipe_read_end(), 0, @"errno %d", errno); }
- (void)testEpollCtlAddSocketpairReadEnd { XCTAssertEqual(epoll_contract_ctl_add_socketpair_read_end(), 0, @"errno %d", errno); }
- (void)testEpollCtlAddDuplicateReturnsExist { XCTAssertEqual(epoll_contract_ctl_add_duplicate_returns_exist(), 0, @"errno %d", errno); }
- (void)testEpollCtlModUpdatesEvents { XCTAssertEqual(epoll_contract_ctl_mod_updates_events(), 0, @"errno %d", errno); }
- (void)testEpollCtlDelRemovesEvents { XCTAssertEqual(epoll_contract_ctl_del_removes_events(), 0, @"errno %d", errno); }
- (void)testEpollWaitPipeReadableAfterWrite { XCTAssertEqual(epoll_contract_wait_pipe_readable_after_write(), 0, @"errno %d", errno); }
- (void)testEpollWaitSocketpairReadableAfterWrite { XCTAssertEqual(epoll_contract_wait_socketpair_readable_after_write(), 0, @"errno %d", errno); }
- (void)testEpollWaitBlocksUntilPipeWrite { XCTAssertEqual(epoll_contract_wait_blocks_until_pipe_write(), 0, @"errno %d", errno); }
- (void)testEpollWaitTimeoutReturnsZero { XCTAssertEqual(epoll_contract_wait_timeout_returns_zero(), 0, @"errno %d", errno); }
- (void)testEpollWaitSignalInterruptReturnsIntr { XCTAssertEqual(epoll_contract_wait_signal_interrupt_returns_intr(), 0, @"errno %d", errno); }
- (void)testEpollRestartSyscallReentersWait { XCTAssertEqual(epoll_contract_restart_syscall_reenters_wait(), 0, @"errno %d", errno); }
- (void)testEpollWaitReportsPipeHupAfterWriterClose { XCTAssertEqual(epoll_contract_wait_reports_pipe_hup_after_writer_close(), 0, @"errno %d", errno); }
- (void)testEpollWaitPtyReadableAfterPeerWrite { XCTAssertEqual(epoll_contract_wait_pty_readable_after_peer_write(), 0, @"errno %d", errno); }
- (void)testEpollFdAppearsInProcSelfFd { XCTAssertEqual(epoll_contract_fd_appears_in_proc_self_fd(), 0, @"errno %d", errno); }
- (void)testEpollFdinfoReportsFlags { XCTAssertEqual(epoll_contract_fdinfo_reports_flags(), 0, @"errno %d", errno); }
- (void)testEpollSyscallSurfaceWaitPipeReadable { XCTAssertEqual(epoll_contract_syscall_surface_wait_pipe_readable(), 0, @"errno %d", errno); }
- (void)testEpollPwaitMaskBlocksSignalUntilPipeReady { XCTAssertEqual(epoll_contract_pwait_mask_blocks_signal_until_pipe_ready(), 0, @"errno %d", errno); }
- (void)testEpollFdinfoReportsWatchedDescriptor { XCTAssertEqual(epoll_contract_fdinfo_reports_watched_descriptor(), 0, @"errno %d", errno); }
- (void)testEpollWaitPidfdReadableAfterTaskExit { XCTAssertEqual(epoll_contract_wait_pidfd_readable_after_task_exit(), 0, @"errno %d", errno); }
- (void)testEpollEdgeTriggerReportsOnceUntilPipeDrained { XCTAssertEqual(epoll_contract_edge_trigger_reports_once_until_pipe_drained(), 0, @"errno %d", errno); }
- (void)testEpollOneshotSuppressesEventsUntilRearmed { XCTAssertEqual(epoll_contract_oneshot_suppresses_events_until_rearmed(), 0, @"errno %d", errno); }

@end

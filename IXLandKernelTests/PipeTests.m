#import <XCTest/XCTest.h>

#include "fs/fdtable.h"
#include "PipeContract.h"
#include "kernel/init.h"
#include "kernel/signal.h"
#include "kernel/task.h"

#include <string.h>

static void reset_pipe_test_kernel_state(void) {
    struct task_struct *child;

    XCTAssertEqual(start_kernel(), 0, @"start_kernel must succeed before LinuxKernel pipe tests");
    XCTAssertTrue(kernel_is_booted(), @"kernel must be booted before LinuxKernel pipe tests");
    XCTAssertNotEqual(init_task, NULL, @"init_task must exist before LinuxKernel pipe tests");

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

@interface PipeTests : XCTestCase
@end

@implementation PipeTests

- (void)setUp {
    [super setUp];
    reset_pipe_test_kernel_state();
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
    reset_pipe_test_kernel_state();
    [super tearDown];
}

- (void)testPipeRejectsNullPipefd { XCTAssertEqual(pipe_contract_rejects_null_pipefd(), 0, @"errno %d", errno); }
- (void)testPipeAllocatesLowestAvailableDescriptors { XCTAssertEqual(pipe_contract_allocates_lowest_available_descriptors(), 0, @"errno %d", errno); }
- (void)testPipeReadEndIsReadableOnly { XCTAssertEqual(pipe_contract_read_end_is_readable_only(), 0, @"errno %d", errno); }
- (void)testPipeWriteEndIsWritableOnly { XCTAssertEqual(pipe_contract_write_end_is_writable_only(), 0, @"errno %d", errno); }
- (void)testPipeRoundTripReadWrite { XCTAssertEqual(pipe_contract_round_trip_read_write(), 0, @"errno %d", errno); }
- (void)testPipeReadConsumesBytesInOrder { XCTAssertEqual(pipe_contract_read_consumes_bytes_in_order(), 0, @"errno %d", errno); }
- (void)testPipeReadEmptyNoWritersReturnsEof { XCTAssertEqual(pipe_contract_read_empty_no_writers_returns_eof(), 0, @"errno %d", errno); }
- (void)testPipeReadEmptyNonblockingReturnsAgain { XCTAssertEqual(pipe_contract_read_empty_nonblocking_returns_again(), 0, @"errno %d", errno); }
- (void)testPipeWriteNoReadersReturnsPipe { XCTAssertEqual(pipe_contract_write_no_readers_returns_pipe(), 0, @"errno %d", errno); }
- (void)testPipe2CloexecSetsBothDescriptors { XCTAssertEqual(pipe_contract_pipe2_cloexec_sets_both_descriptors(), 0, @"errno %d", errno); }
- (void)testPipe2NonblockSetsBothDescriptors { XCTAssertEqual(pipe_contract_pipe2_nonblock_sets_both_descriptors(), 0, @"errno %d", errno); }
- (void)testPipe2RejectsUnsupportedFlags { XCTAssertEqual(pipe_contract_pipe2_rejects_unsupported_flags(), 0, @"errno %d", errno); }
- (void)testPipeDupSharesPipeObject { XCTAssertEqual(pipe_contract_dup_shares_pipe_object(), 0, @"errno %d", errno); }
- (void)testPipeCloseOnExecClosesOnlyFlaggedPipeDescriptor { XCTAssertEqual(pipe_contract_close_on_exec_closes_only_flagged_pipe_descriptor(), 0, @"errno %d", errno); }
- (void)testPipeFstatReportsFifo { XCTAssertEqual(pipe_contract_fstat_reports_fifo(), 0, @"errno %d", errno); }
- (void)testPipeLseekReturnsSpipe { XCTAssertEqual(pipe_contract_lseek_returns_spipe(), 0, @"errno %d", errno); }
- (void)testPipePreadReturnsSpipe { XCTAssertEqual(pipe_contract_pread_returns_spipe(), 0, @"errno %d", errno); }
- (void)testPipePwriteReturnsSpipe { XCTAssertEqual(pipe_contract_pwrite_returns_spipe(), 0, @"errno %d", errno); }
- (void)testPipeGetdentsReturnsNotdir { XCTAssertEqual(pipe_contract_getdents_returns_notdir(), 0, @"errno %d", errno); }
- (void)testPollPipeReadEndReadableWhenDataAvailable { XCTAssertEqual(pipe_contract_poll_read_end_readable_when_data_available(), 0, @"errno %d", errno); }
- (void)testPollPipeReadEndHupWhenNoWriters { XCTAssertEqual(pipe_contract_poll_read_end_hup_when_no_writers(), 0, @"errno %d", errno); }
- (void)testPollPipeWriteEndWritableWhenCapacityAvailable { XCTAssertEqual(pipe_contract_poll_write_end_writable_when_capacity_available(), 0, @"errno %d", errno); }
- (void)testPollInvalidFdReturnsNval { XCTAssertEqual(pipe_contract_poll_invalid_fd_returns_nval(), 0, @"errno %d", errno); }
- (void)testProcSelfFdShowsPipeDescriptor { XCTAssertEqual(pipe_contract_proc_self_fd_shows_pipe_descriptor(), 0, @"errno %d", errno); }
- (void)testProcSelfFdinfoShowsPipeFlags { XCTAssertEqual(pipe_contract_proc_self_fdinfo_shows_pipe_flags(), 0, @"errno %d", errno); }
- (void)testPipeBlockingReadWakesWhenWriterWrites { XCTAssertEqual(pipe_contract_blocking_read_wakes_when_writer_writes(), 0, @"errno %d", errno); }
- (void)testPipeBlockingReadInterruptedBySignal { XCTAssertEqual(pipe_contract_blocking_read_interrupted_by_signal(), 0, @"errno %d", errno); }
- (void)testPipeBlockingWriteWakesWhenReaderDrains { XCTAssertEqual(pipe_contract_blocking_write_wakes_when_reader_drains(), 0, @"errno %d", errno); }
- (void)testPipeBlockingWriteInterruptedBySignalReturnsIntr { XCTAssertEqual(pipe_contract_blocking_write_interrupted_by_signal(), 0, @"errno %d", errno); }
- (void)testPipeWriteNoReadersQueuesSigpipe { XCTAssertEqual(pipe_contract_write_no_readers_queues_sigpipe(), 0, @"errno %d", errno); }
- (void)testPollPipeReadEndBlockingWakesWhenWriterWrites { XCTAssertEqual(pipe_contract_blocking_poll_read_wakes_when_writer_writes(), 0, @"errno %d", errno); }

@end

#import <XCTest/XCTest.h>

#include "fs/fdtable.h"
#include "IXLandSystemLinuxKernelTests/PipeContract.h"

@interface PipeTests : XCTestCase
@end

@implementation PipeTests

- (void)setUp {
    [super setUp];
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

#import <XCTest/XCTest.h>

#include <errno.h>

#include "PtraceContract.h"
#include "kernel/cred_internal.h"
#include "kernel/init.h"

@interface PtraceTests : XCTestCase
@end

@implementation PtraceTests

- (void)setUp {
    [super setUp];
    XCTAssertEqual(start_kernel(), 0);
    cred_reset_to_defaults();
}

- (void)testAttachDetachChildSameUserNamespace {
    XCTAssertEqual(ptrace_contract_attach_detach_child_same_user_namespace(), 0, @"errno %d", errno);
}

- (void)testNewuserChildCannotAttachParentNamespaceTask {
    XCTAssertEqual(ptrace_contract_newuser_child_cannot_attach_parent_namespace_task(), 0, @"errno %d", errno);
}

- (void)testNewuserChildCanAttachSameNamespaceTask {
    XCTAssertEqual(ptrace_contract_newuser_child_can_attach_same_namespace_task(), 0, @"errno %d", errno);
}

- (void)testRegsetRoundTripsGeneralRegisters {
    XCTAssertEqual(ptrace_contract_regset_round_trips_general_registers(), 0, @"errno %d", errno);
}

- (void)testSyscallTraceRecordsEntryAndExit {
    XCTAssertEqual(ptrace_contract_syscall_trace_records_entry_and_exit(), 0, @"errno %d", errno);
}

- (void)testContInjectsPendingSignal {
    XCTAssertEqual(ptrace_contract_cont_injects_pending_signal(), 0, @"errno %d", errno);
}

- (void)testAttachStopIsWaitpidVisible {
    XCTAssertEqual(ptrace_contract_attach_stop_is_waitpid_visible(), 0, @"errno %d", errno);
}

- (void)testSyscallStopIsWaitpidVisible {
    XCTAssertEqual(ptrace_contract_syscall_stop_is_waitpid_visible(), 0, @"errno %d", errno);
}

- (void)testPeekPokeDataUsesVirtualMemory {
    XCTAssertEqual(ptrace_contract_peek_poke_data_uses_virtual_memory(), 0, @"errno %d", errno);
}

- (void)testTracecloneRecordsEventMessage {
    XCTAssertEqual(ptrace_contract_traceclone_records_event_message(), 0, @"errno %d", errno);
}

- (void)testClone3SetTidTracecloneRecordsRequestedPid {
    XCTAssertEqual(ptrace_contract_clone3_set_tid_traceclone_records_requested_pid(), 0, @"errno %d", errno);
}

- (void)testTraceexecRecordsEventMessage {
    XCTAssertEqual(ptrace_contract_traceexec_records_event_message(), 0, @"errno %d", errno);
}

- (void)testTraceexitRecordsEventMessage {
    XCTAssertEqual(ptrace_contract_traceexit_records_event_message(), 0, @"errno %d", errno);
}

- (void)testSignalDeliveryStopCanBeSuppressed {
    XCTAssertEqual(ptrace_contract_signal_delivery_stop_can_be_suppressed(), 0, @"errno %d", errno);
}

- (void)testSignalDeliveryStopCanInjectSignal {
    XCTAssertEqual(ptrace_contract_signal_delivery_stop_can_inject_signal(), 0, @"errno %d", errno);
}

- (void)testEventStopStatusEncodesEvent {
    XCTAssertEqual(ptrace_contract_event_stop_status_encodes_event(), 0, @"errno %d", errno);
}

@end

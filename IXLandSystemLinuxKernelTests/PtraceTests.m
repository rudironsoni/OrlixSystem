#import <XCTest/XCTest.h>

#include <errno.h>

#include "IXLandSystemLinuxKernelTests/PtraceContract.h"
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

@end

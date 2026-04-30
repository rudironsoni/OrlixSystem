#import <XCTest/XCTest.h>

#include <errno.h>

#include "fs/fdtable.h"
#include "kernel/init.h"

extern int exec_syscall_contract_rejects_null_path_without_transition(void);
extern int exec_syscall_contract_rejects_empty_path_without_transition(void);
extern int exec_syscall_contract_missing_path_preserves_state_and_cloexec_fds(void);
extern int exec_syscall_contract_native_success_applies_transition_and_returns_entry_status(void);
extern int exec_syscall_contract_script_uses_virtual_path_and_native_interpreter(void);
extern int exec_syscall_contract_missing_script_interpreter_preserves_state(void);
extern int exec_syscall_contract_fexecve_uses_fd_path(void);
extern int exec_syscall_contract_fexecve_rejects_invalid_fd(void);

@interface ExecSyscallTests : XCTestCase
@end

@implementation ExecSyscallTests

- (void)setUp {
    [super setUp];
    XCTAssertEqual(start_kernel(), 0, @"start_kernel must succeed before LinuxKernel exec-syscall tests");
    XCTAssertTrue(kernel_is_booted(), @"kernel must be booted before LinuxKernel exec-syscall tests");
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

- (void)testRejectsNullPathWithoutTransition {
    XCTAssertEqual(exec_syscall_contract_rejects_null_path_without_transition(), 0, @"errno %d", errno);
}

- (void)testRejectsEmptyPathWithoutTransition {
    XCTAssertEqual(exec_syscall_contract_rejects_empty_path_without_transition(), 0, @"errno %d", errno);
}

- (void)testMissingPathPreservesStateAndCloexecFds {
    XCTAssertEqual(exec_syscall_contract_missing_path_preserves_state_and_cloexec_fds(), 0, @"errno %d", errno);
}

- (void)testNativeSuccessAppliesTransitionAndReturnsEntryStatus {
    XCTAssertEqual(exec_syscall_contract_native_success_applies_transition_and_returns_entry_status(), 0, @"errno %d", errno);
}

- (void)testScriptUsesVirtualPathAndNativeInterpreter {
    XCTAssertEqual(exec_syscall_contract_script_uses_virtual_path_and_native_interpreter(), 0, @"errno %d", errno);
}

- (void)testMissingScriptInterpreterPreservesState {
    XCTAssertEqual(exec_syscall_contract_missing_script_interpreter_preserves_state(), 0, @"errno %d", errno);
}

- (void)testFexecveUsesFdPath {
    XCTAssertEqual(exec_syscall_contract_fexecve_uses_fd_path(), 0, @"errno %d", errno);
}

- (void)testFexecveRejectsInvalidFd {
    XCTAssertEqual(exec_syscall_contract_fexecve_rejects_invalid_fd(), 0, @"errno %d", errno);
}

@end

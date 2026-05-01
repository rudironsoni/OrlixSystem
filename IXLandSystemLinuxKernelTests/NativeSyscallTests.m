#import <XCTest/XCTest.h>

#include <errno.h>

#include "fs/fdtable.h"
#include "kernel/init.h"
#include "IXLandSystemLinuxKernelTests/NativeSyscallContract.h"

@interface NativeSyscallTests : XCTestCase
@end

@implementation NativeSyscallTests

- (void)setUp {
    [super setUp];
    XCTAssertEqual(start_kernel(), 0, @"start_kernel must succeed before native syscall tests");
    XCTAssertTrue(kernel_is_booted(), @"kernel must be booted before native syscall tests");
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

- (void)testDispatchesFdPipeAndProcfs {
    XCTAssertEqual(native_syscall_contract_dispatches_fd_pipe_and_procfs(), 0, @"errno %d", errno);
}

- (void)testDispatchesVmIdentityTimeAndDirs {
    XCTAssertEqual(native_syscall_contract_dispatches_vm_identity_time_and_dirs(), 0, @"errno %d", errno);
}

- (void)testDispatchesProcessStartupSyscalls {
    XCTAssertEqual(native_syscall_contract_dispatches_process_startup_syscalls(), 0, @"errno %d", errno);
}

- (void)testRegistersNativeArtifactDescriptor {
    XCTAssertEqual(native_syscall_contract_registers_native_artifact_descriptor(), 0, @"errno %d", errno);
}

- (void)testExecsSbinInitThroughSyscallSurface {
    XCTAssertEqual(native_syscall_contract_execs_sbin_init_through_syscall_surface(), 0, @"errno %d", errno);
}

- (void)testReturnsRawNegativeErrno {
    XCTAssertEqual(native_syscall_contract_returns_raw_negative_errno(), 0, @"errno %d", errno);
}

- (void)testRegisteredProgramUsesSyscallSurface {
    XCTAssertEqual(native_syscall_contract_registered_program_uses_syscall_surface(), 0, @"errno %d", errno);
}

@end

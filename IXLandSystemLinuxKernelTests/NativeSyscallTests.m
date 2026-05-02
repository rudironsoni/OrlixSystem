#import <XCTest/XCTest.h>

#include <errno.h>

#include "fs/fdtable.h"
#include "kernel/init.h"
#include "IXLandSystemLinuxKernelTests/NativeSyscallContract.h"
#include "IXLandSystemLinuxKernelTests/SignalSyscallContract.h"

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

- (void)testEnforcesVmaFaultPolicy {
    XCTAssertEqual(native_syscall_contract_enforces_vma_fault_policy(), 0, @"errno %d", errno);
}

- (void)testMunmapGapAndMapFixedReplacePolicy {
    XCTAssertEqual(native_syscall_contract_munmap_gap_and_map_fixed_replace_policy(), 0, @"errno %d", errno);
}

- (void)testMremapGrowsAndMovesMapping {
    XCTAssertEqual(native_syscall_contract_mremap_grows_and_moves_mapping(), 0, @"errno %d", errno);
}

- (void)testMadviseDontneedDiscardsPrivatePage {
    XCTAssertEqual(native_syscall_contract_madvise_dontneed_discards_private_page(), 0, @"errno %d", errno);
}

- (void)testMapsSharedFileAndSyncs {
    XCTAssertEqual(native_syscall_contract_maps_shared_file_and_syncs(), 0, @"errno %d", errno);
}

- (void)testMremapExtendsSharedMappingWriteback {
    XCTAssertEqual(native_syscall_contract_mremap_extends_shared_mapping_writeback(), 0, @"errno %d", errno);
}

- (void)testMsyncPreservesCleanSharedPages {
    XCTAssertEqual(native_syscall_contract_msync_preserves_clean_shared_pages(), 0, @"errno %d", errno);
}

- (void)testUnlinkedSharedMappingSyncsThroughOpenFd {
    XCTAssertEqual(native_syscall_contract_unlinked_shared_mapping_syncs_through_open_fd(), 0, @"errno %d", errno);
}

- (void)testSharedFileMappingsAreCoherent {
    XCTAssertEqual(native_syscall_contract_shared_file_mappings_are_coherent(), 0, @"errno %d", errno);
}

- (void)testSharedFileMappingsAreCoherentAcrossReopen {
    XCTAssertEqual(native_syscall_contract_shared_file_mappings_are_coherent_across_reopen(), 0, @"errno %d", errno);
}

- (void)testSharedFileMappingsAreCoherentAcrossHardlink {
    XCTAssertEqual(native_syscall_contract_shared_file_mappings_are_coherent_across_hardlink(), 0, @"errno %d", errno);
}

- (void)testCloneWithoutVmCopiesPrivateVmas {
    XCTAssertEqual(native_syscall_contract_clone_without_vm_copies_private_vmas(), 0, @"errno %d", errno);
}

- (void)testCloneWithoutVmPreservesSharedFileMappings {
    XCTAssertEqual(native_syscall_contract_clone_without_vm_preserves_shared_file_mappings(), 0, @"errno %d", errno);
}

- (void)testFtruncateUpdatesSharedMappingFaultPolicy {
    XCTAssertEqual(native_syscall_contract_ftruncate_updates_shared_mapping_fault_policy(), 0, @"errno %d", errno);
}

- (void)testProcSelfMapsReportsPermissionRuns {
    XCTAssertEqual(native_syscall_contract_proc_self_maps_reports_permission_runs(), 0, @"errno %d", errno);
}

- (void)testDevZeroMmapIsVirtualZeroMemory {
    XCTAssertEqual(native_syscall_contract_dev_zero_mmap_is_virtual_zero_memory(), 0, @"errno %d", errno);
}

- (void)testDispatchesProcessStartupSyscalls {
    XCTAssertEqual(native_syscall_contract_dispatches_process_startup_syscalls(), 0, @"errno %d", errno);
}

- (void)testRtSigactionUsesLinuxUapiLayout {
    XCTAssertEqual(signal_syscall_contract_rt_sigaction_uses_linux_uapi_layout(), 0, @"errno %d", errno);
}

- (void)testSigaltstackAndFramePolicy {
    XCTAssertEqual(signal_syscall_contract_sigaltstack_and_frame_policy(), 0, @"errno %d", errno);
}

- (void)testSignalFrameWritesVirtualRecord {
    XCTAssertEqual(signal_syscall_contract_frame_writes_virtual_record(), 0, @"errno %d", errno);
}

- (void)testSignalFrameRecordsHandlerHandoff {
    XCTAssertEqual(signal_syscall_contract_frame_records_handler_handoff(), 0, @"errno %d", errno);
}

- (void)testSignalFrameRecordsMaskRestorerAndContext {
    XCTAssertEqual(signal_syscall_contract_frame_records_mask_restorer_and_context(), 0, @"errno %d", errno);
}

- (void)testRtSigreturnRestoresMaskAndAltstack {
    XCTAssertEqual(signal_syscall_contract_rt_sigreturn_restores_mask_and_altstack(), 0, @"errno %d", errno);
}

- (void)testRtSigreturnRestoresFrameContextRecord {
    XCTAssertEqual(signal_syscall_contract_rt_sigreturn_restores_frame_context_record(), 0, @"errno %d", errno);
}

- (void)testSignalFrameContainsLinuxUcontext {
    XCTAssertEqual(signal_syscall_contract_frame_contains_linux_ucontext(), 0, @"errno %d", errno);
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

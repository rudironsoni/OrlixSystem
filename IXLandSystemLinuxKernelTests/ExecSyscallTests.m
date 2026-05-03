#import <XCTest/XCTest.h>

#include <errno.h>

#include "fs/fdtable.h"
#include "kernel/init.h"

extern int exec_syscall_contract_rejects_null_path_without_transition(void);
extern int exec_syscall_contract_rejects_empty_path_without_transition(void);
extern int exec_syscall_contract_missing_path_preserves_state_and_cloexec_fds(void);
extern int exec_syscall_contract_native_success_applies_transition_and_returns_entry_status(void);
extern int exec_syscall_contract_native_exec_records_proc_cmdline_and_environ(void);
extern int exec_syscall_contract_native_execve_applies_setuid_setgid_saved_ids(void);
extern int exec_syscall_contract_native_execve_no_new_privs_blocks_setid_saved_ids(void);
extern int exec_syscall_contract_native_execve_applies_file_capability_metadata(void);
extern int exec_syscall_contract_native_execve_no_new_privs_blocks_file_capabilities(void);
extern int exec_syscall_contract_native_execve_no_new_privs_clears_ambient_on_file_caps(void);
extern int exec_syscall_contract_native_execve_secure_noroot_blocks_root_cap_gain(void);
extern int exec_syscall_contract_native_execve_applies_security_capability_xattr(void);
extern int exec_syscall_contract_native_execve_nosuid_mount_blocks_setid_and_file_caps(void);
extern int exec_syscall_contract_oversized_argv_returns_e2big_without_transition(void);
extern int exec_syscall_contract_script_uses_virtual_path_and_native_interpreter(void);
extern int exec_syscall_contract_script_exec_records_interpreter_proc_cmdline(void);
extern int exec_syscall_contract_script_symlink_records_resolved_target(void);
extern int exec_syscall_contract_missing_script_interpreter_preserves_state(void);
extern int exec_syscall_contract_fexecve_uses_fd_path(void);
extern int exec_syscall_contract_fexecve_rejects_invalid_fd(void);
extern int exec_syscall_contract_elf64_aarch64_exec_loads_virtual_image(void);
extern int exec_syscall_contract_elf_program_headers_create_virtual_segments(void);
extern int exec_syscall_contract_elf_interp_loads_virtual_loader_image(void);
extern int exec_syscall_contract_elf_static_builds_initial_stack_and_auxv(void);
extern int exec_syscall_contract_elf_dynamic_auxv_points_to_loader_base(void);
extern int exec_syscall_contract_elf_auxv_records_virtual_credentials(void);
extern int exec_syscall_contract_elf_virtual_memory_writes_writable_segment(void);
extern int exec_syscall_contract_elf_virtual_memory_writes_initial_stack(void);
extern int exec_syscall_contract_elf_virtual_memory_fault_policy(void);
extern int exec_syscall_contract_elf_vma_metadata_covers_exec_loader_and_stack(void);
extern int exec_syscall_contract_elf_below_stack_guard_faults_with_sigsegv_maperr(void);
extern int exec_syscall_contract_elf_stack_grows_down_within_rlimit(void);
extern int exec_syscall_contract_elf_stack_growth_respects_rlimit(void);
extern int exec_syscall_contract_elf_stack_growth_keeps_lower_guard_faulting(void);
extern int exec_syscall_contract_elf_dynamic_metadata_records_exec_and_loader(void);
extern int exec_syscall_contract_elf_exec_handoff_exposes_entry_stack_and_memory_access(void);
extern int exec_syscall_contract_elf_vma_page_permissions_are_page_granular(void);
extern int exec_syscall_contract_elf_dynamic_relocation_metadata_is_discovered(void);
extern int exec_syscall_contract_aarch64_exec_context_uses_exec_handoff(void);
extern int exec_syscall_contract_aarch64_relocations_apply_relative_globdat_and_jumpslot(void);
extern int exec_syscall_contract_mmap_mprotect_and_munmap_update_vmas(void);
extern int exec_syscall_contract_mmap_private_file_write_marks_private_dirty_and_preserves_file(void);
extern int exec_syscall_contract_shared_file_truncate_faults_with_sigbus_bus_adrerr(void);
extern int exec_syscall_contract_aarch64_exec_context_runs_nop_until_brk(void);
extern int exec_syscall_contract_elf_missing_interp_returns_enoent_without_transition(void);
extern int exec_syscall_contract_elf_invalid_interp_returns_enoexec_without_transition(void);
extern int exec_syscall_contract_elf_dyn_image_is_accepted(void);
extern int exec_syscall_contract_elf_interp_without_nul_returns_enoexec_without_transition(void);
extern int exec_syscall_contract_elf_too_many_load_segments_returns_enoexec_without_transition(void);
extern int exec_syscall_contract_elf_bad_load_segment_returns_enoexec_without_transition(void);
extern int exec_syscall_contract_elf_wrong_machine_returns_enoexec_without_transition(void);
extern int exec_syscall_contract_truncated_elf_returns_enoexec_without_transition(void);

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

- (void)testNativeExecRecordsProcCmdlineAndEnviron {
    XCTAssertEqual(exec_syscall_contract_native_exec_records_proc_cmdline_and_environ(), 0, @"errno %d", errno);
}

- (void)testNativeExecveAppliesSetuidSetgidSavedIds {
    XCTAssertEqual(exec_syscall_contract_native_execve_applies_setuid_setgid_saved_ids(), 0, @"errno %d", errno);
}

- (void)testNativeExecveNoNewPrivsBlocksSetidSavedIds {
    XCTAssertEqual(exec_syscall_contract_native_execve_no_new_privs_blocks_setid_saved_ids(), 0, @"errno %d", errno);
}

- (void)testNativeExecveAppliesFileCapabilityMetadata {
    XCTAssertEqual(exec_syscall_contract_native_execve_applies_file_capability_metadata(), 0, @"errno %d", errno);
}

- (void)testNativeExecveNoNewPrivsBlocksFileCapabilities {
    XCTAssertEqual(exec_syscall_contract_native_execve_no_new_privs_blocks_file_capabilities(), 0, @"errno %d", errno);
}

- (void)testNativeExecveNoNewPrivsClearsAmbientOnFileCaps {
    XCTAssertEqual(exec_syscall_contract_native_execve_no_new_privs_clears_ambient_on_file_caps(), 0, @"errno %d", errno);
}

- (void)testNativeExecveSecureNorootBlocksRootCapGain {
    XCTAssertEqual(exec_syscall_contract_native_execve_secure_noroot_blocks_root_cap_gain(), 0, @"errno %d", errno);
}

- (void)testNativeExecveAppliesSecurityCapabilityXattr {
    XCTAssertEqual(exec_syscall_contract_native_execve_applies_security_capability_xattr(), 0, @"errno %d", errno);
}

- (void)testNativeExecveNosuidMountBlocksSetidAndFileCaps {
    XCTAssertEqual(exec_syscall_contract_native_execve_nosuid_mount_blocks_setid_and_file_caps(), 0, @"errno %d", errno);
}

- (void)testOversizedArgvReturnsE2bigWithoutTransition {
    XCTAssertEqual(exec_syscall_contract_oversized_argv_returns_e2big_without_transition(), 0, @"errno %d", errno);
}

- (void)testScriptUsesVirtualPathAndNativeInterpreter {
    XCTAssertEqual(exec_syscall_contract_script_uses_virtual_path_and_native_interpreter(), 0, @"errno %d", errno);
}

- (void)testScriptExecRecordsInterpreterProcCmdline {
    XCTAssertEqual(exec_syscall_contract_script_exec_records_interpreter_proc_cmdline(), 0, @"errno %d", errno);
}

- (void)testScriptSymlinkRecordsResolvedTarget {
    XCTAssertEqual(exec_syscall_contract_script_symlink_records_resolved_target(), 0, @"errno %d", errno);
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

- (void)testElf64Aarch64ExecLoadsVirtualImage {
    XCTAssertEqual(exec_syscall_contract_elf64_aarch64_exec_loads_virtual_image(), 0, @"errno %d", errno);
}

- (void)testElfProgramHeadersCreateVirtualSegments {
    XCTAssertEqual(exec_syscall_contract_elf_program_headers_create_virtual_segments(), 0, @"errno %d", errno);
}

- (void)testElfInterpLoadsVirtualLoaderImage {
    XCTAssertEqual(exec_syscall_contract_elf_interp_loads_virtual_loader_image(), 0, @"errno %d", errno);
}

- (void)testElfStaticBuildsInitialStackAndAuxv {
    XCTAssertEqual(exec_syscall_contract_elf_static_builds_initial_stack_and_auxv(), 0, @"errno %d", errno);
}

- (void)testElfDynamicAuxvPointsToLoaderBase {
    XCTAssertEqual(exec_syscall_contract_elf_dynamic_auxv_points_to_loader_base(), 0, @"errno %d", errno);
}

- (void)testElfAuxvRecordsVirtualCredentials {
    XCTAssertEqual(exec_syscall_contract_elf_auxv_records_virtual_credentials(), 0, @"errno %d", errno);
}

- (void)testElfVirtualMemoryWritesWritableSegment {
    XCTAssertEqual(exec_syscall_contract_elf_virtual_memory_writes_writable_segment(), 0, @"errno %d", errno);
}

- (void)testElfVirtualMemoryWritesInitialStack {
    XCTAssertEqual(exec_syscall_contract_elf_virtual_memory_writes_initial_stack(), 0, @"errno %d", errno);
}

- (void)testElfVirtualMemoryFaultPolicy {
    XCTAssertEqual(exec_syscall_contract_elf_virtual_memory_fault_policy(), 0, @"errno %d", errno);
}

- (void)testElfVmaMetadataCoversExecLoaderAndStack {
    XCTAssertEqual(exec_syscall_contract_elf_vma_metadata_covers_exec_loader_and_stack(), 0, @"errno %d", errno);
}

- (void)testElfBelowStackGuardFaultsWithSigsegvMaperr {
    XCTAssertEqual(exec_syscall_contract_elf_below_stack_guard_faults_with_sigsegv_maperr(), 0, @"errno %d", errno);
}

- (void)testElfStackGrowsDownWithinRlimit {
    XCTAssertEqual(exec_syscall_contract_elf_stack_grows_down_within_rlimit(), 0, @"errno %d", errno);
}

- (void)testElfStackGrowthRespectsRlimit {
    XCTAssertEqual(exec_syscall_contract_elf_stack_growth_respects_rlimit(), 0, @"errno %d", errno);
}

- (void)testElfStackGrowthKeepsLowerGuardFaulting {
    XCTAssertEqual(exec_syscall_contract_elf_stack_growth_keeps_lower_guard_faulting(), 0, @"errno %d", errno);
}

- (void)testElfDynamicMetadataRecordsExecAndLoader {
    XCTAssertEqual(exec_syscall_contract_elf_dynamic_metadata_records_exec_and_loader(), 0, @"errno %d", errno);
}

- (void)testElfExecHandoffExposesEntryStackAndMemoryAccess {
    XCTAssertEqual(exec_syscall_contract_elf_exec_handoff_exposes_entry_stack_and_memory_access(), 0, @"errno %d", errno);
}

- (void)testElfVmaPagePermissionsArePageGranular {
    XCTAssertEqual(exec_syscall_contract_elf_vma_page_permissions_are_page_granular(), 0, @"errno %d", errno);
}

- (void)testElfDynamicRelocationMetadataIsDiscovered {
    XCTAssertEqual(exec_syscall_contract_elf_dynamic_relocation_metadata_is_discovered(), 0, @"errno %d", errno);
}

- (void)testAarch64ExecContextUsesExecHandoff {
    XCTAssertEqual(exec_syscall_contract_aarch64_exec_context_uses_exec_handoff(), 0, @"errno %d", errno);
}

- (void)testAarch64RelocationsApplyRelativeGlobdatAndJumpslot {
    XCTAssertEqual(exec_syscall_contract_aarch64_relocations_apply_relative_globdat_and_jumpslot(), 0, @"errno %d", errno);
}

- (void)testMmapMprotectAndMunmapUpdateVmas {
    XCTAssertEqual(exec_syscall_contract_mmap_mprotect_and_munmap_update_vmas(), 0, @"errno %d", errno);
}

- (void)testMmapPrivateFileWriteMarksPrivateDirtyAndPreservesFile {
    XCTAssertEqual(exec_syscall_contract_mmap_private_file_write_marks_private_dirty_and_preserves_file(), 0, @"errno %d", errno);
}

- (void)testSharedFileTruncateFaultsWithSigbusBusAdrerr {
    XCTAssertEqual(exec_syscall_contract_shared_file_truncate_faults_with_sigbus_bus_adrerr(), 0, @"errno %d", errno);
}

- (void)testAarch64ExecContextRunsNopUntilBrk {
    XCTAssertEqual(exec_syscall_contract_aarch64_exec_context_runs_nop_until_brk(), 0, @"errno %d", errno);
}

- (void)testElfMissingInterpReturnsEnoentWithoutTransition {
    XCTAssertEqual(exec_syscall_contract_elf_missing_interp_returns_enoent_without_transition(), 0, @"errno %d", errno);
}

- (void)testElfInvalidInterpReturnsEnoexecWithoutTransition {
    XCTAssertEqual(exec_syscall_contract_elf_invalid_interp_returns_enoexec_without_transition(), 0, @"errno %d", errno);
}

- (void)testElfDynImageIsAccepted {
    XCTAssertEqual(exec_syscall_contract_elf_dyn_image_is_accepted(), 0, @"errno %d", errno);
}

- (void)testElfInterpWithoutNulReturnsEnoexecWithoutTransition {
    XCTAssertEqual(exec_syscall_contract_elf_interp_without_nul_returns_enoexec_without_transition(), 0, @"errno %d", errno);
}

- (void)testElfTooManyLoadSegmentsReturnsEnoexecWithoutTransition {
    XCTAssertEqual(exec_syscall_contract_elf_too_many_load_segments_returns_enoexec_without_transition(), 0, @"errno %d", errno);
}

- (void)testElfBadLoadSegmentReturnsEnoexecWithoutTransition {
    XCTAssertEqual(exec_syscall_contract_elf_bad_load_segment_returns_enoexec_without_transition(), 0, @"errno %d", errno);
}

- (void)testElfWrongMachineReturnsEnoexecWithoutTransition {
    XCTAssertEqual(exec_syscall_contract_elf_wrong_machine_returns_enoexec_without_transition(), 0, @"errno %d", errno);
}

- (void)testTruncatedElfReturnsEnoexecWithoutTransition {
    XCTAssertEqual(exec_syscall_contract_truncated_elf_returns_enoexec_without_transition(), 0, @"errno %d", errno);
}

@end

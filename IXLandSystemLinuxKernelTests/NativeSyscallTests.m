#import <XCTest/XCTest.h>

#include <errno.h>

#include "fs/fdtable.h"
#include "kernel/init.h"
#include "IXLandSystemLinuxKernelTests/NativeSyscallContract.h"
#include "IXLandSystemLinuxKernelTests/SignalSyscallContract.h"
#include "IXLandSystemLinuxKernelTests/SyscallUioContract.h"

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

- (void)testMapFixedNoreplaceReusesUnmappedGapOnly {
    XCTAssertEqual(native_syscall_contract_map_fixed_noreplace_reuses_unmapped_gap_only(), 0, @"errno %d", errno);
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

- (void)testPrivateFileMappingMsyncDoesNotWriteBackCow {
    XCTAssertEqual(native_syscall_contract_private_file_mapping_msync_does_not_write_back_cow(), 0, @"errno %d", errno);
}

- (void)testUnlinkedSharedMappingSyncsThroughOpenFd {
    XCTAssertEqual(native_syscall_contract_unlinked_shared_mapping_syncs_through_open_fd(), 0, @"errno %d", errno);
}

- (void)testSharedMappingSurvivesFdCloseAndSyncs {
    XCTAssertEqual(native_syscall_contract_shared_mapping_survives_fd_close_and_syncs(), 0, @"errno %d", errno);
}

- (void)testUnlinkedSharedMappingSurvivesFdCloseAndSyncs {
    XCTAssertEqual(native_syscall_contract_unlinked_shared_mapping_survives_fd_close_and_syncs(), 0, @"errno %d", errno);
}

- (void)testSecurityCapabilityXattrRoundTrips {
    XCTAssertEqual(native_syscall_contract_security_capability_xattr_round_trips(), 0, @"errno %d", errno);
}

- (void)testUserXattrListRemoveRoundTrips {
    XCTAssertEqual(native_syscall_contract_user_xattr_list_remove_round_trips(), 0, @"errno %d", errno);
}

- (void)testFlistxattrReportsFdUserAttribute {
    XCTAssertEqual(native_syscall_contract_flistxattr_reports_fd_user_attribute(), 0, @"errno %d", errno);
}

- (void)testLxattrTargetsSymlinkInode {
    XCTAssertEqual(native_syscall_contract_lxattr_targets_symlink_inode(), 0, @"errno %d", errno);
}

- (void)testXattrLifetimeTracksRenameExchangeAndSymlinkUnlink {
    XCTAssertEqual(native_syscall_contract_xattr_lifetime_tracks_rename_exchange_and_symlink_unlink(), 0, @"errno %d", errno);
}

- (void)testSharedMappingFaultPolicyTracksTruncateAfterFdClose {
    XCTAssertEqual(native_syscall_contract_shared_mapping_fault_policy_tracks_truncate_after_fd_close(), 0, @"errno %d", errno);
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

- (void)testSharedHardlinkMappingFaultPolicyTracksTruncateAfterOriginalUnlink {
    XCTAssertEqual(native_syscall_contract_shared_hardlink_mapping_fault_policy_tracks_truncate_after_original_unlink(), 0,
                   @"errno %d", errno);
}

- (void)testCloneWithoutVmCopiesPrivateVmas {
    XCTAssertEqual(native_syscall_contract_clone_without_vm_copies_private_vmas(), 0, @"errno %d", errno);
}

- (void)testCloneWithoutVmCowsPrivateFileMapping {
    XCTAssertEqual(native_syscall_contract_clone_without_vm_cows_private_file_mapping(), 0, @"errno %d", errno);
}

- (void)testPrivateFileCowSmapsReportsAnonymousDirtyPage {
    XCTAssertEqual(native_syscall_contract_private_file_cow_smaps_reports_anonymous_dirty_page(), 0, @"errno %d", errno);
}

- (void)testSmapsSplitsMprotectRunsAndPreservesDirtyCounts {
    XCTAssertEqual(native_syscall_contract_smaps_splits_mprotect_runs_and_preserves_dirty_counts(), 0, @"errno %d", errno);
}

- (void)testMapFixedGapCoalescesCompatibleAnonymousVmas {
    XCTAssertEqual(native_syscall_contract_map_fixed_gap_coalesces_compatible_anonymous_vmas(), 0, @"errno %d", errno);
}

- (void)testPrivateFileCowSmapsSurvivesMunmapGap {
    XCTAssertEqual(native_syscall_contract_private_file_cow_smaps_survives_munmap_gap(), 0, @"errno %d", errno);
}

- (void)testPrivateFileCowSurvivesTruncateAndCleanPageFaults {
    XCTAssertEqual(native_syscall_contract_private_file_cow_survives_truncate_and_clean_page_faults(), 0, @"errno %d", errno);
}

- (void)testPartialTruncateZeroFillsAndMincoreTracksPages {
    XCTAssertEqual(native_syscall_contract_partial_truncate_zero_fills_and_mincore_tracks_pages(), 0, @"errno %d", errno);
}

- (void)testPartialPageMsyncAndSharedGrowthWriteback {
    XCTAssertEqual(native_syscall_contract_partial_page_msync_and_shared_growth_writeback(), 0, @"errno %d", errno);
}

- (void)testRenameUpdatesOpenFdAndMappingIdentity {
    XCTAssertEqual(native_syscall_contract_rename_updates_open_fd_and_mapping_identity(), 0, @"errno %d", errno);
}

- (void)testMremapFixedPreservesPrivateFileCowSmaps {
    XCTAssertEqual(native_syscall_contract_mremap_fixed_preserves_private_file_cow_smaps(), 0, @"errno %d", errno);
}

- (void)testMremapFixedCoalescesFileBackedNeighbors {
    XCTAssertEqual(native_syscall_contract_mremap_fixed_coalesces_file_backed_neighbors(), 0, @"errno %d", errno);
}

- (void)testMremapFixedRejectsZeroTarget {
    XCTAssertEqual(native_syscall_contract_mremap_fixed_rejects_zero_target(), 0, @"errno %d", errno);
}

- (void)testMremapFixedRejectsOverlappingTarget {
    XCTAssertEqual(native_syscall_contract_mremap_fixed_rejects_overlapping_target(), 0, @"errno %d", errno);
}

- (void)testMremapFixedGrowPreservesSharedFileMapping {
    XCTAssertEqual(native_syscall_contract_mremap_fixed_grow_preserves_shared_file_mapping(), 0, @"errno %d", errno);
}

- (void)testMremapShrinkPreservesAccountingAndUnmapsTail {
    XCTAssertEqual(native_syscall_contract_mremap_shrink_preserves_accounting_and_unmaps_tail(), 0, @"errno %d", errno);
}

- (void)testMovedSharedMappingTruncateUpdatesFaultMincoreAndSmaps {
    XCTAssertEqual(native_syscall_contract_moved_shared_mapping_truncate_updates_fault_mincore_and_smaps(), 0, @"errno %d", errno);
}

- (void)testMadviseSplitVmaClearsEachPermissionRun {
    XCTAssertEqual(native_syscall_contract_madvise_split_vma_clears_each_permission_run(), 0, @"errno %d", errno);
}

- (void)testPrivateFileMadviseDontneedRestoresFilePageAfterCow {
    XCTAssertEqual(native_syscall_contract_private_file_madvise_dontneed_restores_file_page_after_cow(), 0, @"errno %d", errno);
}

- (void)testMprotectFileSmapsVmflagsFollowPermissionRuns {
    XCTAssertEqual(native_syscall_contract_mprotect_file_smaps_vmflags_follow_permission_runs(), 0, @"errno %d", errno);
}

- (void)testVmaSplitChainPreservesPermissionRuns {
    XCTAssertEqual(native_syscall_contract_vma_split_chain_preserves_permission_runs(), 0, @"errno %d", errno);
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

- (void)testVirtualMemoryFaultsQueueSigsegvCodes {
    XCTAssertEqual(native_syscall_contract_virtual_memory_faults_queue_sigsegv_codes(), 0, @"errno %d", errno);
}

- (void)testProtNoneReadFaultQueuesSigsegvAccerr {
    XCTAssertEqual(native_syscall_contract_prot_none_read_fault_queues_sigsegv_accerr(), 0, @"errno %d", errno);
}

- (void)testStackGuardWriteGrowsAndBelowGuardFaults {
    XCTAssertEqual(native_syscall_contract_stack_guard_write_grows_and_below_guard_faults(), 0, @"errno %d", errno);
}

- (void)testPartialCopyRecordsSigbusFaultAddress {
    XCTAssertEqual(native_syscall_contract_partial_copy_records_sigbus_fault_address(), 0, @"errno %d", errno);
}

- (void)testPartialCopyRecordsFaultAddress {
    XCTAssertEqual(native_syscall_contract_partial_copy_records_fault_address(), 0, @"errno %d", errno);
}

- (void)testProcPidMapsAndStatusReflectChildTask {
    XCTAssertEqual(native_syscall_contract_proc_pid_maps_and_status_reflect_child_task(), 0, @"errno %d", errno);
}

- (void)testProcVmAccountingReportsMappedPages {
    XCTAssertEqual(native_syscall_contract_proc_vm_accounting_reports_mapped_pages(), 0, @"errno %d", errno);
}

- (void)testProcStatusReportsVmHighWaterFields {
    XCTAssertEqual(native_syscall_contract_proc_status_reports_vm_high_water_fields(), 0, @"errno %d", errno);
}

- (void)testProcSelfSmapsReportsVmaAccounting {
    XCTAssertEqual(native_syscall_contract_proc_self_smaps_reports_vma_accounting(), 0, @"errno %d", errno);
}

- (void)testProcSelfSmapsDirtyClearsAfterMadvise {
    XCTAssertEqual(native_syscall_contract_proc_self_smaps_dirty_clears_after_madvise(), 0, @"errno %d", errno);
}

- (void)testProcSelfSmapsReclaimsDontneedResidency {
    XCTAssertEqual(native_syscall_contract_proc_self_smaps_reclaims_dontneed_residency(), 0, @"errno %d", errno);
}

- (void)testMincoreReportsVirtualResidency {
    XCTAssertEqual(native_syscall_contract_mincore_reports_virtual_residency(), 0, @"errno %d", errno);
}

- (void)testReadFaultRestoresMincoreResidency {
    XCTAssertEqual(native_syscall_contract_read_fault_restores_mincore_residency(), 0, @"errno %d", errno);
}

- (void)testMincoreUsesFileOffsetForTruncateResidency {
    XCTAssertEqual(native_syscall_contract_mincore_uses_file_offset_for_truncate_residency(), 0, @"errno %d", errno);
}

- (void)testTruncatedFileMappingFaultQueuesSigbus {
    XCTAssertEqual(native_syscall_contract_truncated_file_mapping_fault_queues_sigbus(), 0, @"errno %d", errno);
}

- (void)testTruncatedFileMappingWriteFaultQueuesSigbus {
    XCTAssertEqual(native_syscall_contract_truncated_file_mapping_write_fault_queues_sigbus(), 0, @"errno %d", errno);
}

- (void)testDispatchesProcessStartupSyscalls {
    XCTAssertEqual(native_syscall_contract_dispatches_process_startup_syscalls(), 0, @"errno %d", errno);
}

- (void)testDispatchesShellFdVfsSyscalls {
    XCTAssertEqual(native_syscall_contract_dispatches_shell_fd_vfs_syscalls(), 0, @"errno %d", errno);
}

- (void)testDispatchesStatxSyscall {
    XCTAssertEqual(native_syscall_contract_dispatches_statx_syscall(), 0, @"errno %d", errno);
}

- (void)testDispatchesExitAndWaitidSyscalls {
    XCTAssertEqual(native_syscall_contract_dispatches_exit_and_waitid_syscalls(), 0, @"errno %d", errno);
}

- (void)testDispatchesReadvWritevSyscalls {
    XCTAssertEqual(syscall_uio_contract_readv_writev_round_trip(), 0, @"errno %d", errno);
}

- (void)testReadvRejectsInvalidIovCount {
    XCTAssertEqual(syscall_uio_contract_rejects_invalid_iov_count(), 0, @"errno %d", errno);
}

- (void)testMlibcLinuxSysdepsInventoryIsKernelOwned {
    XCTAssertEqual(native_syscall_contract_mlibc_linux_sysdeps_inventory_is_kernel_owned(), 0, @"errno %d", errno);
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

- (void)testIgnoredSignalDispositionsDoNotQueueOrTerminate {
    XCTAssertEqual(signal_syscall_contract_ignored_dispositions_do_not_queue_or_terminate(), 0, @"errno %d", errno);
}

- (void)testRealtimeSignalQueuePreservesMultiplicityAndOrder {
    XCTAssertEqual(signal_syscall_contract_realtime_queue_preserves_multiplicity_and_order(), 0, @"errno %d", errno);
}

- (void)testSignalFrameAppliesHandlerMaskNodeferAndResethand {
    XCTAssertEqual(signal_syscall_contract_frame_applies_handler_mask_nodefer_and_resethand(), 0, @"errno %d", errno);
}

- (void)testRestartMetadataFollowsSaRestart {
    XCTAssertEqual(signal_syscall_contract_restart_metadata_follows_sa_restart(), 0, @"errno %d", errno);
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

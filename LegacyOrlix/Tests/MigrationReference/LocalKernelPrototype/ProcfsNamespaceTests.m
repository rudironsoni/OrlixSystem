#import <XCTest/XCTest.h>

#include <errno.h>

#include "ProcfsNamespaceContract.h"
#include "kernel/init.h"

@interface ProcfsNamespaceTests : XCTestCase
@end

@implementation ProcfsNamespaceTests

- (void)setUp {
    [super setUp];
    XCTAssertEqual(start_kernel(), 0, @"start_kernel must succeed before procfs namespace tests");
    XCTAssertTrue(kernel_is_booted(), @"kernel must be booted before procfs namespace tests");
}

- (void)testNamespaceDirectoryOpens {
    XCTAssertEqual(procfs_namespace_contract_ns_directory_opens(), 0, @"errno %d", errno);
}

- (void)testNamespaceLinksAreLinuxShaped {
    XCTAssertEqual(procfs_namespace_contract_ns_links_are_linux_shaped(), 0, @"errno %d", errno);
}

- (void)testUnshareNewutsChangesUtsLink {
    XCTAssertEqual(procfs_namespace_contract_unshare_newuts_changes_uts_link(), 0, @"errno %d", errno);
}

- (void)testUnshareNewnsChangesMntLink {
    XCTAssertEqual(procfs_namespace_contract_unshare_newns_changes_mnt_link(), 0, @"errno %d", errno);
}

- (void)testProcPidStatusAliasesCurrentTask {
    XCTAssertEqual(procfs_namespace_contract_proc_pid_status_aliases_current_task(), 0, @"errno %d", errno);
}

- (void)testPidNamespaceStatusReportsNspid {
    XCTAssertEqual(procfs_namespace_contract_pid_namespace_status_reports_nspid(), 0, @"errno %d", errno);
}

- (void)testProcPidStatusReportsTargetCredentials {
    XCTAssertEqual(procfs_namespace_contract_proc_pid_status_reports_target_credentials(), 0, @"errno %d", errno);
}

- (void)testProcStatusReportsGroupsAndCapabilities {
    XCTAssertEqual(procfs_namespace_contract_proc_status_reports_groups_and_capabilities(), 0, @"errno %d", errno);
}

- (void)testProcStatusReportsThreadAndSignalQueueFields {
    XCTAssertEqual(procfs_namespace_contract_proc_status_reports_thread_and_signal_queue_fields(), 0, @"errno %d", errno);
}

- (void)testProcStatusReportsThreadGroupCount {
    XCTAssertEqual(procfs_namespace_contract_proc_status_reports_thread_group_count(), 0, @"errno %d", errno);
}

- (void)testProcTaskTidStatusAliasesThread {
    XCTAssertEqual(procfs_namespace_contract_proc_task_tid_status_aliases_thread(), 0, @"errno %d", errno);
}

- (void)testProcTaskTidFdMapsAndStatAreThreadTargeted {
    XCTAssertEqual(procfs_namespace_contract_proc_task_tid_fd_maps_and_stat_are_thread_targeted(), 0, @"errno %d", errno);
}

- (void)testCloneFilesSharesThreadFdtable {
    XCTAssertEqual(procfs_namespace_contract_clone_files_shares_thread_fdtable(), 0, @"errno %d", errno);
}

- (void)testProcTaskTidStatusReportsThreadSignalState {
    XCTAssertEqual(procfs_namespace_contract_proc_task_tid_status_reports_thread_signal_state(), 0, @"errno %d", errno);
}

- (void)testCloneVmThreadSharesProcMaps {
    XCTAssertEqual(procfs_namespace_contract_clone_vm_thread_shares_proc_maps(), 0, @"errno %d", errno);
}

- (void)testProcessSignalReportsSharedPending {
    XCTAssertEqual(procfs_namespace_contract_process_signal_reports_shared_pending(), 0, @"errno %d", errno);
}

- (void)testThreadSignalPendingIsPerTid {
    XCTAssertEqual(procfs_namespace_contract_thread_signal_pending_is_per_tid(), 0, @"errno %d", errno);
}

- (void)testThreadGroupStopContinueReportsOnce {
    XCTAssertEqual(procfs_namespace_contract_thread_group_stop_continue_reports_once(), 0, @"errno %d", errno);
}

- (void)testProcPidStatCwdAndExeReportTargetTask {
    XCTAssertEqual(procfs_namespace_contract_proc_pid_stat_cwd_and_exe_report_target_task(), 0, @"errno %d", errno);
}

- (void)testProcPidFdAndFdinfoPathsAreTargetAware {
    XCTAssertEqual(procfs_namespace_contract_proc_pid_fd_and_fdinfo_paths_are_target_aware(), 0, @"errno %d", errno);
}

- (void)testProcPidFdDirListsTargetInheritedFdsAfterParentClose {
    XCTAssertEqual(procfs_namespace_contract_proc_pid_fd_dir_lists_target_inherited_fds_after_parent_close(), 0, @"errno %d", errno);
}

- (void)testFdinfoFlagsArePerTaskDescriptorState {
    XCTAssertEqual(procfs_namespace_contract_fdinfo_flags_are_per_task_descriptor_state(), 0, @"errno %d", errno);
}

- (void)testChildCloseDoesNotCloseParentDescriptor {
    XCTAssertEqual(procfs_namespace_contract_child_close_does_not_close_parent_descriptor(), 0, @"errno %d", errno);
}

- (void)testFdinfoOffsetTracksSharedOpenFileDescription {
    XCTAssertEqual(procfs_namespace_contract_fdinfo_offset_tracks_shared_open_file_description(), 0, @"errno %d", errno);
}

- (void)testProcPidCmdlineEnvironAndCommReportTargetTask {
    XCTAssertEqual(procfs_namespace_contract_proc_pid_cmdline_environ_and_comm_report_target_task(), 0, @"errno %d", errno);
}

- (void)testProcPidStatStatusAndMapsReportTargetTask {
    XCTAssertEqual(procfs_namespace_contract_proc_pid_stat_status_and_maps_report_target_task(), 0, @"errno %d", errno);
}

- (void)testProcPidStatusStatAndFdinfoHaveLinuxFields {
    XCTAssertEqual(procfs_namespace_contract_proc_pid_status_stat_and_fdinfo_have_linux_fields(), 0, @"errno %d", errno);
}

- (void)testProcPidStatReportsTtyStartRssAndExitSignal {
    XCTAssertEqual(procfs_namespace_contract_proc_pid_stat_reports_tty_start_rss_and_exit_signal(), 0, @"errno %d", errno);
}

- (void)testProcPidViewsRemainConsistentAcrossLifecycle {
    XCTAssertEqual(procfs_namespace_contract_proc_pid_views_remain_consistent_across_lifecycle(), 0, @"errno %d", errno);
}

- (void)testProcPidMountinfoAndMountsUseTargetMountNamespace {
    XCTAssertEqual(procfs_namespace_contract_proc_pid_mountinfo_uses_target_mount_namespace(), 0, @"errno %d", errno);
}

- (void)testZombieProcPidMountsKeepTargetMountNamespace {
    XCTAssertEqual(procfs_namespace_contract_zombie_proc_pid_mounts_keep_target_mount_namespace(), 0, @"errno %d", errno);
}

- (void)testReapedProcPidMountsDisappear {
    XCTAssertEqual(procfs_namespace_contract_reaped_proc_pid_mounts_disappear(), 0, @"errno %d", errno);
}

- (void)testReapedProcPidCoreViewsDisappear {
    XCTAssertEqual(procfs_namespace_contract_reaped_proc_pid_core_views_disappear(), 0, @"errno %d", errno);
}

- (void)testRootProcFilesAreReadable {
    XCTAssertEqual(procfs_namespace_contract_root_files_are_readable(), 0, @"errno %d", errno);
}

- (void)testNamespaceDirectoryListsCgroup {
    XCTAssertEqual(procfs_namespace_contract_ns_directory_lists_cgroup(), 0, @"errno %d", errno);
}

- (void)testCgroupNamespaceLinkChangesAfterUnshare {
    XCTAssertEqual(procfs_namespace_contract_cgroup_namespace_link_changes_after_unshare(), 0, @"errno %d", errno);
}

@end

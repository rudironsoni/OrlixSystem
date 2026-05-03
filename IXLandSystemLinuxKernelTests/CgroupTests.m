#import <XCTest/XCTest.h>

#include <errno.h>

#include "kernel/init.h"
#include "kernel/cred_internal.h"
#include "IXLandSystemLinuxKernelTests/CgroupContract.h"

@interface CgroupTests : XCTestCase
@end

@implementation CgroupTests

- (void)setUp {
    [super setUp];
    XCTAssertEqual(start_kernel(), 0);
    cred_reset_to_defaults();
}

- (void)testCurrentTaskStartsInRootCgroup {
    XCTAssertEqual(cgroup_contract_current_task_starts_in_root(), 0,
                   @"current task should be a member of virtual root cgroup, errno %d", errno);
}

- (void)testChildInheritsParentCgroup {
    XCTAssertEqual(cgroup_contract_child_inherits_parent_cgroup(), 0,
                   @"child tasks should inherit parent virtual cgroup membership, errno %d", errno);
}

- (void)testProcSelfCgroupReportsRoot {
    XCTAssertEqual(cgroup_contract_proc_self_cgroup_reports_root(), 0,
                   @"/proc/self/cgroup should expose cgroup v2 root membership, errno %d", errno);
}

- (void)testCgroupfsCreatesGroupAndMovesCurrentTask {
    XCTAssertEqual(cgroup_contract_cgroupfs_creates_group_and_moves_current_task(), 0,
                   @"cgroupfs should create a group and move the current task through cgroup.procs, errno %d", errno);
}

- (void)testCgroupfsMovesChildAndProcPidReportsMembership {
    XCTAssertEqual(cgroup_contract_cgroupfs_moves_child_and_proc_pid_reports_membership(), 0,
                   @"cgroupfs should move a child task and /proc/<pid>/cgroup should report it, errno %d", errno);
}

- (void)testCgroupNamespaceRebasesProcAndCgroupfsVisibility {
    XCTAssertEqual(cgroup_contract_cgroup_namespace_rebases_proc_and_cgroupfs_visibility(), 0,
                   @"cgroup namespace should rebase /proc cgroup paths and cgroupfs visibility, errno %d", errno);
}

- (void)testPidsControllerTracksCurrentAndMax {
    XCTAssertEqual(cgroup_contract_pids_controller_tracks_current_and_max(), 0,
                   @"pids controller should expose pids.current and pids.max, errno %d", errno);
}

- (void)testPidsMaxRejectsExtraMigration {
    XCTAssertEqual(cgroup_contract_pids_max_rejects_extra_migration(), 0,
                   @"pids.max should reject migrations beyond the limit, errno %d", errno);
}

- (void)testFreezerBlocksAndReleasesMigration {
    XCTAssertEqual(cgroup_contract_freezer_blocks_and_releases_migration(), 0,
                   @"cgroup.freeze should block and release task migration, errno %d", errno);
}

- (void)testSubtreeControlAcceptsPidsAndFreezer {
    XCTAssertEqual(cgroup_contract_subtree_control_accepts_pids_and_freezer(), 0,
                   @"cgroup.subtree_control should accept pids and freezer controllers, errno %d", errno);
}

- (void)testCgroupNamespaceOpenFdSurvivesResetUntilClosed {
    XCTAssertEqual(cgroup_contract_cgroup_namespace_open_fd_survives_reset_until_closed(), 0,
                   @"open cgroupfs fd should keep its cgroup identity across namespace reset, errno %d", errno);
}

- (void)testMountCgroup2ExposesCgroupfsView {
    XCTAssertEqual(cgroup_contract_mount_cgroup2_exposes_cgroupfs_view(), 0,
                   @"mounted cgroup2 should expose cgroupfs through the VFS mount graph, errno %d", errno);
}

- (void)testRmdirEmptyCgroupRemovesFromHierarchy {
    XCTAssertEqual(cgroup_contract_rmdir_empty_cgroup_removes_from_hierarchy(), 0,
                   @"rmdir should remove an empty cgroup from cgroupfs, errno %d", errno);
}

- (void)testRmdirBusyCgroupFails {
    XCTAssertEqual(cgroup_contract_rmdir_busy_cgroup_fails(), 0,
                   @"rmdir should reject cgroups with member tasks, errno %d", errno);
}

- (void)testRmdirParentWithChildFailsNotempty {
    XCTAssertEqual(cgroup_contract_rmdir_parent_with_child_fails_notempty(), 0,
                   @"rmdir should reject cgroups with child cgroups, errno %d", errno);
}

- (void)testNewuserCapsAreScopedToCgroupNamespaceOwner {
    XCTAssertEqual(cgroup_contract_newuser_caps_are_scoped_to_cgroup_namespace_owner(), 0,
                   @"cgroup control should require CAP_SYS_ADMIN in cgroup namespace owner user namespace, errno %d", errno);
}

- (void)testCgroupNamespaceRejectsMigrationOfHiddenTask {
    XCTAssertEqual(cgroup_contract_cgroup_namespace_rejects_migration_of_hidden_task(), 0,
                   @"cgroup namespace should reject migration of tasks outside its visible subtree, errno %d", errno);
}

- (void)testProcPidCgroupHidesTasksOutsideReaderNamespace {
    XCTAssertEqual(cgroup_contract_proc_pid_cgroup_hides_tasks_outside_reader_namespace(), 0,
                   @"/proc/<pid>/cgroup should not leak cgroups outside the reader namespace, errno %d", errno);
}

- (void)testNestedCgroupNamespaceRebasesVisibility {
    XCTAssertEqual(cgroup_contract_nested_cgroup_namespace_rebases_visibility(), 0,
                   @"nested cgroup namespaces should rebase procfs and cgroupfs visibility at each root, errno %d", errno);
}

- (void)testReapedTaskReleasesCgroupMembership {
    XCTAssertEqual(cgroup_contract_reaped_task_releases_cgroup_membership(), 0,
                   @"reaped task should be removed from cgroup.procs and pids.current, errno %d", errno);
}

@end

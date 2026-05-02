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

@end

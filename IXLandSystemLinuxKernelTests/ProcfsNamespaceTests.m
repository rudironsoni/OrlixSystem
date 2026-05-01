#import <XCTest/XCTest.h>

#include <errno.h>

#include "IXLandSystemLinuxKernelTests/ProcfsNamespaceContract.h"
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

@end

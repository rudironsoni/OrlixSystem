#import <XCTest/XCTest.h>

#include <errno.h>

#include "NamespaceContract.h"
#include "kernel/init.h"

@interface NamespaceTests : XCTestCase
@end

@implementation NamespaceTests

- (void)setUp {
    [super setUp];
    XCTAssertEqual(start_kernel(), 0, @"start_kernel must succeed before namespace tests");
    XCTAssertTrue(kernel_is_booted(), @"kernel must be booted before namespace tests");
}

- (void)testCloneNewutsIsolatesChildHostname {
    XCTAssertEqual(namespace_contract_clone_newuts_isolates_child_hostname(), 0, @"errno %d", errno);
}

- (void)testCloneWithoutNewutsSharesHostname {
    XCTAssertEqual(namespace_contract_clone_without_newuts_shares_hostname(), 0, @"errno %d", errno);
}

- (void)testUnshareNewutsIsolatesCurrentTask {
    XCTAssertEqual(namespace_contract_unshare_newuts_isolates_current_task(), 0, @"errno %d", errno);
}

- (void)testCloneNewnsIsolatesChildMounts {
    XCTAssertEqual(namespace_contract_clone_newns_isolates_child_mounts(), 0, @"errno %d", errno);
}

- (void)testCloneWithoutNewnsSharesMounts {
    XCTAssertEqual(namespace_contract_clone_without_newns_shares_mounts(), 0, @"errno %d", errno);
}

- (void)testUnshareNewnsIsolatesCurrentMounts {
    XCTAssertEqual(namespace_contract_unshare_newns_isolates_current_mounts(), 0, @"errno %d", errno);
}

- (void)testUnshareCloneFsSplitsSharedFsState {
    XCTAssertEqual(namespace_contract_unshare_clone_fs_splits_shared_fs_state(), 0, @"errno %d", errno);
}

- (void)testCloneNewnsWithCloneFsRejected {
    XCTAssertEqual(namespace_contract_clone_newns_with_clone_fs_rejected(), 0, @"errno %d", errno);
}

- (void)testCloneNewpidRecordsChildNamespaceMetadata {
    XCTAssertEqual(namespace_contract_clone_newpid_records_child_namespace_metadata(), 0, @"errno %d", errno);
}

- (void)testUnshareNewpidAppliesToNextChildMetadata {
    XCTAssertEqual(namespace_contract_unshare_newpid_applies_to_next_child_metadata(), 0, @"errno %d", errno);
}

- (void)testNewuserCapsAreScopedToMountNamespaceOwner {
    XCTAssertEqual(namespace_contract_newuser_caps_are_scoped_to_mount_namespace_owner(), 0, @"errno %d", errno);
}

- (void)testProcUidGidMapsAreVisible {
    XCTAssertEqual(namespace_contract_proc_uid_gid_maps_are_visible(), 0, @"errno %d", errno);
}

- (void)testProcUidGidMapsAreWritableWithSetgroupsPolicy {
    XCTAssertEqual(namespace_contract_proc_uid_gid_maps_are_writable_with_setgroups_policy(), 0, @"errno %d", errno);
}

- (void)testNewuserCapsAreScopedToUtsNamespaceOwner {
    XCTAssertEqual(namespace_contract_newuser_caps_are_scoped_to_uts_namespace_owner(), 0, @"errno %d", errno);
}

@end

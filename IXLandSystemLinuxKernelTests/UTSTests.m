#import <XCTest/XCTest.h>

#include <errno.h>

#include "IXLandSystemLinuxKernelTests/UTSContract.h"
#include "kernel/init.h"

@interface UTSTests : XCTestCase
@end

@implementation UTSTests

- (void)setUp {
    [super setUp];
    XCTAssertEqual(start_kernel(), 0, @"start_kernel must succeed before UTS tests");
    XCTAssertTrue(kernel_is_booted(), @"kernel must be booted before UTS tests");
    uts_contract_reset_state();
}

- (void)tearDown {
    uts_contract_reset_state();
    [super tearDown];
}

- (void)testUnameReportsLinuxShape {
    XCTAssertEqual(uts_contract_uname_reports_linux_shape(), 0, @"errno %d", errno);
}

- (void)testSethostnameUpdatesUnameAndGethostname {
    XCTAssertEqual(uts_contract_sethostname_updates_uname_and_gethostname(), 0, @"errno %d", errno);
}

- (void)testSetdomainnameUpdatesUnameAndGetdomainname {
    XCTAssertEqual(uts_contract_setdomainname_updates_uname_and_getdomainname(), 0, @"errno %d", errno);
}

- (void)testSethostnameRejectsOversizedName {
    XCTAssertEqual(uts_contract_sethostname_rejects_oversized_name(), 0, @"errno %d", errno);
}

- (void)testNonrootCannotSethostname {
    XCTAssertEqual(uts_contract_nonroot_cannot_sethostname(), 0, @"errno %d", errno);
}

- (void)testChildInheritsParentUtsNamespace {
    XCTAssertEqual(uts_contract_child_inherits_parent_uts_namespace(), 0, @"errno %d", errno);
}

- (void)testUnshareIsolatesChildUtsNamespace {
    XCTAssertEqual(uts_contract_unshare_isolates_child_uts_namespace(), 0, @"errno %d", errno);
}

@end

#import <XCTest/XCTest.h>

#include <errno.h>

#include "IXLandSystemLinuxKernelTests/DevfsContract.h"
#include "kernel/init.h"

@interface DevfsTests : XCTestCase
@end

@implementation DevfsTests

- (void)setUp {
    [super setUp];
    XCTAssertEqual(start_kernel(), 0, @"start_kernel must succeed before devfs tests");
    XCTAssertTrue(kernel_is_booted(), @"kernel must be booted before devfs tests");
}

- (void)testRandomDeviceIsCharacterAndReadable {
    XCTAssertEqual(devfs_contract_random_device_is_character_and_readable(), 0, @"errno %d", errno);
}

- (void)testTtyNodeExistsWithoutControllingTty {
    XCTAssertEqual(devfs_contract_tty_node_exists_without_controlling_tty(), 0, @"errno %d", errno);
}

- (void)testTtyOpenWithoutControllingTtyReturnsEnxio {
    XCTAssertEqual(devfs_contract_tty_open_without_controlling_tty_returns_enxio(), 0, @"errno %d", errno);
}

- (void)testPtsDirectoryExists {
    XCTAssertEqual(devfs_contract_pts_directory_exists(), 0, @"errno %d", errno);
}

- (void)testDevDirectoryListsLinuxDeviceNodes {
    XCTAssertEqual(devfs_contract_dev_directory_lists_linux_device_nodes(), 0, @"errno %d", errno);
}

- (void)testAllocatedPtySlaveIsVisibleInDevpts {
    XCTAssertEqual(devfs_contract_allocated_pty_slave_is_visible_in_devpts(), 0, @"errno %d", errno);
}

@end

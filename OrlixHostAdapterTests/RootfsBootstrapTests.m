/*
 * Host bridge Rootfs bootstrap tests
 * These tests prove host bridge file access only.
 * Linux-visible rootfs translation/shape proof belongs in OrlixKernelTests.
 */

#import <XCTest/XCTest.h>
#import "HostTestSupport.h"

@interface RootfsBootstrapTests : XCTestCase
@end

@implementation RootfsBootstrapTests

- (void)setUp {
    [super setUp];
}

- (void)tearDown {
    [super tearDown];
}

- (void)testVirtualEtcPasswdExists {
    int fd = host_test_backing_open_readonly("/etc/passwd");
    XCTAssertTrue(fd >= 0, @"backing_open /etc/passwd via host bridge should succeed");
    if (fd >= 0) host_test_backing_close(fd);
}

- (void)testVirtualEtcGroupExists {
    int fd = host_test_backing_open_readonly("/etc/group");
    XCTAssertTrue(fd >= 0, @"backing_open /etc/group via host bridge should succeed");
    if (fd >= 0) host_test_backing_close(fd);
}

- (void)testVirtualEtcHostsExists {
    int fd = host_test_backing_open_readonly("/etc/hosts");
    XCTAssertTrue(fd >= 0, @"backing_open /etc/hosts via host bridge should succeed");
    if (fd >= 0) host_test_backing_close(fd);
}

- (void)testVirtualEtcResolvConfExists {
    int fd = host_test_backing_open_readonly("/etc/resolv.conf");
    XCTAssertTrue(fd >= 0, @"backing_open /etc/resolv.conf via host bridge should succeed");
    if (fd >= 0) host_test_backing_close(fd);
}

@end

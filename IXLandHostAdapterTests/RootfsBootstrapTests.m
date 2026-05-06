/*
 * Host bridge Rootfs bootstrap tests
 * These tests require host file operations and therefore live in the
 * IXLandHostAdapterTests target.
 */

#import <XCTest/XCTest.h>

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "IXLandHostAdapter/fs/backing_io_decls.h"
#include "fs/vfs.h"

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
    char host_path[MAX_PATH];
    int ret = vfs_translate_path("/etc/passwd", host_path, sizeof(host_path));

    XCTAssertEqual(ret, 0, @"vfs_translate_path for /etc/passwd should succeed");

    /* Verify file is accessible by opening it via host bridge */
    int fd = host_open_impl(host_path, O_RDONLY, 0);
    XCTAssertTrue(fd >= 0, @"host_open_impl /etc/passwd via host bridge should succeed");
    if (fd >= 0) host_close_impl(fd);
}

- (void)testVirtualEtcGroupExists {
    char host_path[MAX_PATH];
    int ret = vfs_translate_path("/etc/group", host_path, sizeof(host_path));

    XCTAssertEqual(ret, 0, @"vfs_translate_path for /etc/group should succeed");

    int fd = host_open_impl(host_path, O_RDONLY, 0);
    XCTAssertTrue(fd >= 0, @"host_open_impl /etc/group via host bridge should succeed");
    if (fd >= 0) host_close_impl(fd);
}

- (void)testVirtualEtcHostsExists {
    char host_path[MAX_PATH];
    int ret = vfs_translate_path("/etc/hosts", host_path, sizeof(host_path));

    XCTAssertEqual(ret, 0, @"vfs_translate_path for /etc/hosts should succeed");

    int fd = host_open_impl(host_path, O_RDONLY, 0);
    XCTAssertTrue(fd >= 0, @"host_open_impl /etc/hosts via host bridge should succeed");
    if (fd >= 0) host_close_impl(fd);
}

- (void)testVirtualEtcResolvConfExists {
    char host_path[MAX_PATH];
    int ret = vfs_translate_path("/etc/resolv.conf", host_path, sizeof(host_path));

    XCTAssertEqual(ret, 0, @"vfs_translate_path for /etc/resolv.conf should succeed");

    int fd = host_open_impl(host_path, O_RDONLY, 0);
    XCTAssertTrue(fd >= 0, @"host_open_impl /etc/resolv.conf via host bridge should succeed");
    if (fd >= 0) host_close_impl(fd);
}

@end

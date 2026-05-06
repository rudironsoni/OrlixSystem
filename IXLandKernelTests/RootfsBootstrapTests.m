/*
 * IXLandSystemTests - RootfsBootstrapTests.m
 *
 * INTERNAL RUNTIME SEMANTIC TEST.
 *
 * Tests the virtual Linux rootfs bootstrap, verifying that
 * identity/config baseline files are created and accessible
 * through IXLand's path mediation.
 */

#import <XCTest/XCTest.h>

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "fs/vfs.h"
#include "fs/fdtable.h"

/* Owner API declarations used by this Linux-only test target. */
int open_impl(const char *pathname, int flags, mode_t mode);
ssize_t read_impl(int fd, void *buf, size_t count);

@interface RootfsBootstrapTests : XCTestCase
@end

@implementation RootfsBootstrapTests

- (void)setUp {
    [super setUp];
}

- (void)tearDown {
    [super tearDown];
}

/*
 * Verify that virtual /etc/passwd exists and is readable
 * through IXLand's path mediation.
 */
- (void)testVirtualEtcPasswdExists {
    char host_path[MAX_PATH];
    int ret = vfs_translate_path(@"/etc/passwd".UTF8String, host_path, sizeof(host_path));

    XCTAssertEqual(ret, 0, @"vfs_translate_path for /etc/passwd should succeed");

    /* Verify file is accessible by opening it via IXLand's owner open_impl/read_impl */
    int fd = open_impl(@"/etc/passwd".UTF8String, O_RDONLY, 0);
    XCTAssertTrue(fd >= 0, @"open_impl /etc/passwd through IXLand should succeed");
    if (fd >= 0) {
        close_impl(fd);
    }
}

/*
 * Verify that virtual /etc/group exists and is readable
 * through IXLand's path mediation.
 */
- (void)testVirtualEtcGroupExists {
    char host_path[MAX_PATH];
    int ret = vfs_translate_path(@"/etc/group".UTF8String, host_path, sizeof(host_path));

    XCTAssertEqual(ret, 0, @"vfs_translate_path for /etc/group should succeed");

    int fd = open_impl(@"/etc/group".UTF8String, O_RDONLY, 0);
    XCTAssertTrue(fd >= 0, @"open_impl /etc/group through IXLand should succeed");
    if (fd >= 0) {
        close_impl(fd);
    }
}

/*
 * Verify that virtual /etc/hosts exists and is readable
 * through IXLand's path mediation.
 */
- (void)testVirtualEtcHostsExists {
    char host_path[MAX_PATH];
    int ret = vfs_translate_path(@"/etc/hosts".UTF8String, host_path, sizeof(host_path));

    XCTAssertEqual(ret, 0, @"vfs_translate_path for /etc/hosts should succeed");

    int fd = open_impl(@"/etc/hosts".UTF8String, O_RDONLY, 0);
    XCTAssertTrue(fd >= 0, @"open_impl /etc/hosts through IXLand should succeed");
    if (fd >= 0) {
        close_impl(fd);
    }
}

/*
 * Verify that virtual /etc/resolv.conf exists and is readable
 * through IXLand's path mediation.
 */
- (void)testVirtualEtcResolvConfExists {
    char host_path[MAX_PATH];
    int ret = vfs_translate_path(@"/etc/resolv.conf".UTF8String, host_path, sizeof(host_path));

    XCTAssertEqual(ret, 0, @"vfs_translate_path for /etc/resolv.conf should succeed");

    int fd = open_impl(@"/etc/resolv.conf".UTF8String, O_RDONLY, 0);
    XCTAssertTrue(fd >= 0, @"open_impl /etc/resolv.conf through IXLand should succeed");
    if (fd >= 0) {
        close_impl(fd);
    }
}

/*
 * Verify that /etc/passwd has Linux-shaped content
 * (at minimum: root user entry with valid fields)
 */
- (void)testVirtualEtcPasswdContent {
    /* Use owner open_impl/read_impl/close_impl to avoid forward-declaring host syscalls */
    int fd = open_impl(@"/etc/passwd".UTF8String, O_RDONLY, 0);
    XCTAssertTrue(fd >= 0, @"open_impl /etc/passwd should succeed");
    if (fd < 0) return;

    char buf[4096];
    ssize_t n = read_impl(fd, buf, sizeof(buf) - 1);
    close_impl(fd);

    XCTAssertTrue(n > 0, @"read /etc/passwd should return content, n=%zd", n);
    if (n <= 0) return;

    buf[n] = '\0';

    /* Verify root user entry exists */
    XCTAssertTrue(strstr(buf, "root") != NULL,
                  @"/etc/passwd should contain root user entry");

    /* Verify ixland user entry exists */
    XCTAssertTrue(strstr(buf, "ixland:x:1000:1000:") != NULL,
                  @"/etc/passwd should contain ixland user entry");
}

/*
 * Verify that /etc/group has Linux-shaped content
 * (at minimum: root group entry with valid fields)
 */
- (void)testVirtualEtcGroupContent {
    int fd = open_impl(@"/etc/group".UTF8String, O_RDONLY, 0);
    XCTAssertTrue(fd >= 0, @"open_impl /etc/group should succeed");
    if (fd < 0) return;

    char buf[4096];
    ssize_t n = read_impl(fd, buf, sizeof(buf) - 1);
    close_impl(fd);

    XCTAssertTrue(n > 0, @"read /etc/group should return content");
    if (n <= 0) return;

    buf[n] = '\0';

    /* Verify root group entry exists */
    XCTAssertTrue(strstr(buf, "root:x:0:") != NULL,
                  @"/etc/group should contain root group entry");

    /* Verify ixland group entry exists */
    XCTAssertTrue(strstr(buf, "ixland:x:1000:") != NULL,
                  @"/etc/group should contain ixland group entry");
}

@end

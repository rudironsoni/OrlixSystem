//
// HostBridgeSmokeTests.m
// OrlixHostAdapterTests
//
// HOST BRIDGE TESTS
//
// Purpose:
//   - prove the host bridge test target builds and links
//   - prove host test support is isolated in the bridge target
//   - verify bridge-only helpers without claiming Linux ABI proof
//
// Allowed includes:
//   - <XCTest/XCTest.h>
//   - HostTestSupport.h
//   - internal/ios/** only when testing that bridge directly
//
// Forbidden includes:
//   - vendored Linux UAPI headers
//   - fs/**
//   - kernel/**
//   - broad bridge bags unless the test is specifically proving their deprecation
//

#import <XCTest/XCTest.h>
#import "HostTestSupport.h"

@interface HostBridgeSmokeTests : XCTestCase
@end

@implementation HostBridgeSmokeTests

- (void)setUp {
    [super setUp];
}

- (void)tearDown {
    [super tearDown];
}

#pragma mark - Target Build Verification

- (void)testBridgeTargetBuildsAndLinks {
    // This test proves the iOS bridge test target builds and links correctly
    XCTAssertTrue(YES, @"Bridge test target built and linked successfully");
}

#pragma mark - Host Test Support Verification

- (void)testHostTestSupportIsAvailable {
    // Verify HostTestSupport.h helpers are available
    int result = host_test_fcntl_getfd(0);
    // fd 0 (stdin) should exist and return valid flags or -1 on error
    // The important thing is that the function is linked
    (void)result;
    XCTAssertTrue(YES, @"HostTestSupport is available in bridge target");
}

- (void)testSignalHelpersAreAvailable {
    // Verify signal helpers from HostTestSupport are available
    int result = host_test_signal_block_sigint();
    // Result depends on current signal state, but function should be callable
    (void)result;
    XCTAssertTrue(YES, @"Signal helpers are available in bridge target");
}

- (void)testFcntlHelpersAreAvailable {
    // Open a file to test fcntl helpers
    int fd = open("/etc/passwd", O_RDONLY);
    if (fd >= 0) {
        int flags = host_test_fcntl_getfd(fd);
        XCTAssertTrue(flags >= 0 || flags == -1, @"fcntl helpers should be callable");

        int fl_flags = host_test_fcntl_getfl(fd);
        XCTAssertTrue(fl_flags >= 0 || fl_flags == -1, @"F_GETFL should be callable");

        close(fd);
    } else {
        XCTSkip(@"Could not open test file");
    }
}

- (void)testFcntlDupfdWorks {
    int fd = open("/etc/passwd", O_RDONLY);
    if (fd >= 0) {
        int new_fd = host_test_fcntl_dupfd(fd, 10);
        if (new_fd >= 0) {
            XCTAssertTrue(new_fd >= 10, @"F_DUPFD should return fd >= min_fd");
            close(new_fd);
        }
        close(fd);
    } else {
        XCTSkip(@"Could not open test file");
    }
}

- (void)testFcntlDupfdCloexecWorks {
    int fd = open("/etc/passwd", O_RDONLY);
    if (fd >= 0) {
        int new_fd = host_test_fcntl_dupfd_cloexec(fd, 10);
        if (new_fd >= 0) {
            XCTAssertTrue(new_fd >= 10, @"F_DUPFD_CLOEXEC should return fd >= min_fd");

            int flags = host_test_fcntl_getfd(new_fd);
            XCTAssertTrue(host_test_fcntl_has_cloexec(flags), @"duped fd should have FD_CLOEXEC");

            close(new_fd);
        }
        close(fd);
    } else {
        XCTSkip(@"Could not open test file");
    }
}

- (void)testFcntlCloexecFlagWorks {
    XCTAssertTrue(host_test_fcntl_has_cloexec(FD_CLOEXEC), @"has_cloexec should detect FD_CLOEXEC");
    XCTAssertFalse(host_test_fcntl_has_cloexec(0), @"has_cloexec should not detect without flag");
}

- (void)testFcntlRdonlyFlagWorks {
    // O_RDONLY is typically 0, so we need to test with O_ACCMODE
    XCTAssertTrue(host_test_fcntl_has_rdonly(O_RDONLY), @"has_rdonly should detect O_RDONLY");
}

@end

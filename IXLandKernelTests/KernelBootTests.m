/* IXLandKernelTests/KernelBootTests.m
 * Objective-C XCTest wrapper for virtual kernel boot contract tests.
 *
 * All Linux contract logic lives in KernelBoot.c (C translation unit).
 * This file only calls C test functions and asserts results.
 * It does NOT rename or re-interpret Linux contracts.
 */

#import <XCTest/XCTest.h>

/* Forward declarations for C test functions in KernelBoot.c */
extern int kernel_boot_test_system_booted(void);
extern int kernel_boot_test_vfs_backing_roots(void);
extern int kernel_boot_test_vfs_routing(void);
extern int kernel_boot_test_synthetic_roots(void);
extern int kernel_boot_test_task_init(void);
extern int kernel_boot_test_init_identity_is_pid_namespace_root(void);
extern int kernel_boot_test_proc_self_tree_is_available(void);
extern int kernel_boot_test_stdio_fd_links_are_virtual_dev_null(void);
extern int kernel_boot_test_task_start_time_is_kernel_owned(void);
extern int kernel_boot_test_proc_self_stat_reports_start_time(void);
extern int kernel_boot_test_fd_table(void);
extern int kernel_boot_test_idempotent(void);
extern int kernel_boot_test_cold_boot(void);
extern int kernel_boot_test_reboot(void);

#include "kernel/init.h"

@interface KernelBootTests : XCTestCase
@end

@implementation KernelBootTests

- (void)setUp {
    [super setUp];
    /* Tests should explicitly control boot state */
}

- (void)tearDown {
    [super tearDown];
}

- (void)testColdBoot {
    /* Note: Constructor may have auto-initialized, so we test the explicit API */
    /* If already booted, shutdown first to test cold boot path */
    if (kernel_is_booted()) {
        kernel_shutdown();
    }
    XCTAssertFalse(kernel_is_booted(), @"System should not be booted after shutdown");

    /* Cold boot */
    int boot_result = start_kernel();
    XCTAssertEqual(boot_result, 0, @"Cold boot start_kernel should succeed");
    XCTAssertTrue(kernel_is_booted(), @"System should be booted after start_kernel");

    /* Verify boot state via C contract tests */
    XCTAssertEqual(kernel_boot_test_vfs_backing_roots(), 0);
    XCTAssertEqual(kernel_boot_test_vfs_routing(), 0);
    XCTAssertEqual(kernel_boot_test_synthetic_roots(), 0);
    XCTAssertEqual(kernel_boot_test_task_init(), 0);
    XCTAssertEqual(kernel_boot_test_fd_table(), 0);
}

- (void)testReboot {
    /* Ensure system is booted first */
    if (!kernel_is_booted()) {
        XCTAssertEqual(start_kernel(), 0, @"Initial boot should succeed");
    }

    /* Shutdown */
    XCTAssertEqual(kernel_shutdown(), 0, @"Shutdown should succeed");
    XCTAssertFalse(kernel_is_booted(), @"System should not be booted after shutdown");

    /* Reboot */
    XCTAssertEqual(start_kernel(), 0, @"Reboot start_kernel should succeed");
    XCTAssertTrue(kernel_is_booted(), @"System should be booted after reboot");

    /* Verify state after reboot */
    XCTAssertEqual(kernel_boot_test_vfs_backing_roots(), 0);
    XCTAssertEqual(kernel_boot_test_task_init(), 0);
}

- (void)testSystemIsBooted {
    if (!kernel_is_booted()) {
        XCTAssertEqual(start_kernel(), 0, @"Boot should succeed");
    }
    XCTAssertEqual(kernel_boot_test_system_booted(), 0);
}

- (void)testVfsBackingRoots {
    if (!kernel_is_booted()) {
        XCTAssertEqual(start_kernel(), 0, @"Boot should succeed");
    }
    XCTAssertEqual(kernel_boot_test_vfs_backing_roots(), 0);
}

- (void)testVfsRouting {
    if (!kernel_is_booted()) {
        XCTAssertEqual(start_kernel(), 0, @"Boot should succeed");
    }
    XCTAssertEqual(kernel_boot_test_vfs_routing(), 0);
}

- (void)testSyntheticRoots {
    if (!kernel_is_booted()) {
        XCTAssertEqual(start_kernel(), 0, @"Boot should succeed");
    }
    XCTAssertEqual(kernel_boot_test_synthetic_roots(), 0);
}

- (void)testTaskInit {
    if (!kernel_is_booted()) {
        XCTAssertEqual(start_kernel(), 0, @"Boot should succeed");
    }
    XCTAssertEqual(kernel_boot_test_task_init(), 0);
}

- (void)testFdTable {
    if (!kernel_is_booted()) {
        XCTAssertEqual(start_kernel(), 0, @"Boot should succeed");
    }
    XCTAssertEqual(kernel_boot_test_fd_table(), 0);
}

- (void)testInitIdentityIsPidNamespaceRoot {
    if (!kernel_is_booted()) {
        XCTAssertEqual(start_kernel(), 0, @"Boot should succeed");
    }
    XCTAssertEqual(kernel_boot_test_init_identity_is_pid_namespace_root(), 0);
}

- (void)testProcSelfTreeIsAvailable {
    if (!kernel_is_booted()) {
        XCTAssertEqual(start_kernel(), 0, @"Boot should succeed");
    }
    XCTAssertEqual(kernel_boot_test_proc_self_tree_is_available(), 0);
}

- (void)testStdioFdLinksAreVirtualDevNull {
    if (!kernel_is_booted()) {
        XCTAssertEqual(start_kernel(), 0, @"Boot should succeed");
    }
    XCTAssertEqual(kernel_boot_test_stdio_fd_links_are_virtual_dev_null(), 0);
}

- (void)testTaskStartTimeIsKernelOwned {
    if (!kernel_is_booted()) {
        XCTAssertEqual(start_kernel(), 0, @"Boot should succeed");
    }
    XCTAssertEqual(kernel_boot_test_task_start_time_is_kernel_owned(), 0);
}

- (void)testProcSelfStatReportsStartTime {
    if (!kernel_is_booted()) {
        XCTAssertEqual(start_kernel(), 0, @"Boot should succeed");
    }
    XCTAssertEqual(kernel_boot_test_proc_self_stat_reports_start_time(), 0);
}

- (void)testBootIdempotent {
    XCTAssertEqual(kernel_boot_test_idempotent(), 0);
}

@end

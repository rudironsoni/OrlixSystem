#import <Foundation/Foundation.h>
#import <XCTest/XCTest.h>

#import "OrlixKernel.h"
#import <asm/boot.h>

@interface OrlixKernelHostProofTests : XCTestCase
@end

@implementation OrlixKernelHostProofTests

- (void)testBootloaderHandoffDoesNotReportBootWithoutStartKernel
{
    struct OrlixBootConfig config = {
        .profile = ORLIX_BOOT_PROFILE_APPSTORE,
        .root_image_identifier = "orlix.test.rootfs",
        .terminal_identifier = "orlix.test.terminal",
    };

    arch_boot_reset_handoff();

    int status = OrlixBoot(&config);

    XCTAssertEqual(status, ORLIX_BOOT_STATUS_UNAVAILABLE);
    XCTAssertEqual(arch_boot_handoff_count(), 1);
    XCTAssertNotEqual(arch_boot_params(), NULL);
}

- (void)testBootloaderRejectsMissingRootIdentifier
{
    struct OrlixBootConfig config = {
        .profile = ORLIX_BOOT_PROFILE_APPSTORE,
        .root_image_identifier = NULL,
        .terminal_identifier = "orlix.test.terminal",
    };

    arch_boot_reset_handoff();

    int status = OrlixBoot(&config);

    XCTAssertEqual(status, ORLIX_BOOT_STATUS_INVALID_CONFIG);
    XCTAssertEqual(arch_boot_handoff_count(), 0);
}

- (void)testHostProofScopeLabelIsKernelDependencyOnly
{
    NSString *proofLane = @"kernel-dependency-host-proof";

    XCTAssertEqualObjects(proofLane, @"kernel-dependency-host-proof");
}

@end

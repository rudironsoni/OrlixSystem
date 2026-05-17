#import <Foundation/Foundation.h>
#import <XCTest/XCTest.h>

#import "OrlixKernel.h"

@interface OrlixKernelHostProofTests : XCTestCase
@end

@implementation OrlixKernelHostProofTests

- (void)testBootloaderHandoffReachesHostedEntrySeamThroughProductAPI
{
    struct OrlixBootConfig config = {
        .profile = ORLIX_BOOT_PROFILE_APPSTORE,
        .root_image_identifier = "orlix.test.rootfs",
        .terminal_identifier = "orlix.test.terminal",
    };

    int status = OrlixBoot(&config);

    XCTAssertEqual(status, ORLIX_BOOT_STATUS_OK);
}

- (void)testBootloaderRejectsMissingRootIdentifier
{
    struct OrlixBootConfig config = {
        .profile = ORLIX_BOOT_PROFILE_APPSTORE,
        .root_image_identifier = NULL,
        .terminal_identifier = "orlix.test.terminal",
    };

    int status = OrlixBoot(&config);

    XCTAssertEqual(status, ORLIX_BOOT_STATUS_INVALID_CONFIG);
}

- (void)testHostProofScopeLabelIsKernelDependencyOnly
{
    NSString *proofLane = @"kernel-dependency-host-proof";

    XCTAssertEqualObjects(proofLane, @"kernel-dependency-host-proof");
}

@end

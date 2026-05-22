#import <Foundation/Foundation.h>
#import <XCTest/XCTest.h>

#import "OrlixKernel.h"

@interface OrlixKernelHostProofTests : XCTestCase
@end

@implementation OrlixKernelHostProofTests

- (void)testBootloaderRejectsUnavailableRootIdentifier
{
    struct OrlixBootConfig config = {
        .profile = ORLIX_BOOT_PROFILE_APPSTORE,
        .root_image_identifier = "orlix.test.rootfs",
        .terminal_identifier = "orlix.test.terminal",
    };

    int status = OrlixBoot(&config);

    XCTAssertEqual(status, ORLIX_BOOT_STATUS_INVALID_CONFIG);
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

@end

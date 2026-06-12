#import <Foundation/Foundation.h>
#import <XCTest/XCTest.h>

#import "OrlixKernel.h"

extern int orlix_host_resources_clear_root_images(void);
extern int orlix_host_resources_register_root_image_files(
    const char *identifier,
    const char *initrd_bundle_name,
    const char *initrd_bundle_extension,
    const char *initrd_resource,
    const char *base_block_path,
    const char *state_block_path,
    unsigned int base_block_device,
    unsigned int state_block_device,
    unsigned long long state_block_minimum_bytes);

@interface OrlixKernelHostProofTests : XCTestCase
@end

@implementation OrlixKernelHostProofTests

- (void)testBootloaderRejectsUnavailableRootIdentifier
{
    struct OrlixBootConfig config = {
        .profile = ORLIX_BOOT_PROFILE_RELEASE,
        .root_image_identifier = "orlix.test.rootfs",
        .terminal_identifier = "orlix.test.terminal",
    };

    int status = OrlixBoot(&config);

    XCTAssertEqual(status, ORLIX_BOOT_STATUS_INVALID_CONFIG);
}

- (void)testBootloaderSelectsMaterializedEnvironmentBlockImagesBeforeInitrdLoad
{
    NSURL *root = [NSURL fileURLWithPath:NSTemporaryDirectory() isDirectory:YES];
    root = [root URLByAppendingPathComponent:NSUUID.UUID.UUIDString
                                 isDirectory:YES];
    NSFileManager *fileManager = NSFileManager.defaultManager;
    XCTAssertTrue([fileManager createDirectoryAtURL:root
                        withIntermediateDirectories:YES
                                         attributes:nil
                                              error:nil]);

    NSURL *baseURL = [root URLByAppendingPathComponent:@"base.ext4"];
    NSURL *stateURL = [root URLByAppendingPathComponent:@"state.ext4"];
    NSMutableData *base = [NSMutableData dataWithLength:512];
    NSMutableData *state = [NSMutableData dataWithLength:512];
    ((unsigned char *)base.mutableBytes)[0] = 0xe1;
    ((unsigned char *)state.mutableBytes)[0] = 0x5e;
    XCTAssertTrue([base writeToURL:baseURL atomically:YES]);
    XCTAssertTrue([state writeToURL:stateURL atomically:YES]);

    XCTAssertEqual(orlix_host_resources_clear_root_images(), 0);
    XCTAssertEqual(
        orlix_host_resources_register_root_image_files(
            "orlix.env.boot",
            "",
            "",
            "rootfs/initramfs.cpio.gz",
            baseURL.fileSystemRepresentation,
            stateURL.fileSystemRepresentation,
            0,
            1,
            1024),
        0);

    struct OrlixBootConfig config = {
        .profile = ORLIX_BOOT_PROFILE_RELEASE,
        .root_image_identifier = "orlix.env.boot",
        .terminal_identifier = "orlix.test.terminal",
    };

    int status = OrlixBoot(&config);
    XCTAssertEqual(status, ORLIX_BOOT_STATUS_INVALID_CONFIG);

    [fileManager removeItemAtURL:root error:nil];
    XCTAssertEqual(orlix_host_resources_clear_root_images(), 0);
}

- (void)testBootloaderRejectsMissingRootIdentifier
{
    struct OrlixBootConfig config = {
        .profile = ORLIX_BOOT_PROFILE_RELEASE,
        .root_image_identifier = NULL,
        .terminal_identifier = "orlix.test.terminal",
    };

    int status = OrlixBoot(&config);

    XCTAssertEqual(status, ORLIX_BOOT_STATUS_INVALID_CONFIG);
}

@end

#import <Foundation/Foundation.h>
#import <XCTest/XCTest.h>

#include "OrlixHostAdapter/boot/resources.h"
#include "OrlixHostAdapter/memory/kernel_mapping.h"

#include <mach/mach.h>
#include <mach/vm_page_size.h>
#include <stdlib.h>
#include <string.h>

#ifndef ORLIX_HOST_ADAPTER_TEST_LINUX_PAGE_SIZE
#error ORLIX_HOST_ADAPTER_TEST_LINUX_PAGE_SIZE must come from target settings.
#endif

unsigned long OrlixHostEnterHostTls(void)
{
    return 0;
}

void OrlixHostLeaveHostTls(unsigned long active_tls)
{
    (void)active_tls;
}

@interface OrlixHostAdapterTests : XCTestCase
@end

@implementation OrlixHostAdapterTests

- (void)testUserMappingAdaptsLinuxPageInsideHostPage
{
    const unsigned long linuxPageSize = ORLIX_HOST_ADAPTER_TEST_LINUX_PAGE_SIZE;
    vm_address_t reserved = 0;
    kern_return_t status = vm_allocate(mach_task_self(),
                                       &reserved,
                                       (vm_size_t)vm_page_size,
                                       VM_FLAGS_ANYWHERE);
    XCTAssertEqual(status, KERN_SUCCESS);
    XCTAssertNotEqual(reserved, (vm_address_t)0);
    if (status != KERN_SUCCESS || !reserved) {
        return;
    }

    status = vm_deallocate(mach_task_self(), reserved, (vm_size_t)vm_page_size);
    XCTAssertEqual(status, KERN_SUCCESS);

    unsigned char *source = malloc((size_t)linuxPageSize);
    XCTAssertTrue(source != NULL);
    if (!source) {
        return;
    }
    memset(source, 0x5a, (size_t)linuxPageSize);

    unsigned long target = (unsigned long)reserved;
    if (vm_page_size > linuxPageSize) {
        target += linuxPageSize;
    }

    int ret = orlix_host_user_map_page(target,
                                       source,
                                       linuxPageSize,
                                       1,
                                       0);
    XCTAssertEqual(ret, 0);
    unsigned char mappedByte = ((volatile unsigned char *)target)[0];
    XCTAssertEqual(mappedByte, 0x5a);

    ((volatile unsigned char *)target)[0] = 0xa5;
    orlix_host_user_sync_writable_mappings();
    XCTAssertEqual(source[0], 0xa5);

    orlix_host_user_unmap_pages(target, linuxPageSize);
    free(source);
}

- (void)testUserWindowRefreshCopiesMultipleLinuxPagesInsideHostPage
{
    if (vm_page_size < 8192) {
        return;
    }

    const unsigned long linuxPageSize = ORLIX_HOST_ADAPTER_TEST_LINUX_PAGE_SIZE;
    vm_address_t reserved = 0;
    kern_return_t status = vm_allocate(mach_task_self(),
                                       &reserved,
                                       (vm_size_t)vm_page_size,
                                       VM_FLAGS_ANYWHERE);
    XCTAssertEqual(status, KERN_SUCCESS);
    XCTAssertNotEqual(reserved, (vm_address_t)0);
    if (status != KERN_SUCCESS || !reserved) {
        return;
    }

    status = vm_deallocate(mach_task_self(), reserved, (vm_size_t)vm_page_size);
    XCTAssertEqual(status, KERN_SUCCESS);

    unsigned char *first = malloc((size_t)linuxPageSize);
    unsigned char *second = malloc((size_t)linuxPageSize);
    XCTAssertTrue(first != NULL);
    XCTAssertTrue(second != NULL);
    if (!first || !second) {
        free(first);
        free(second);
        return;
    }
    memset(first, 0x11, (size_t)linuxPageSize);
    memset(second, 0x22, (size_t)linuxPageSize);

    struct orlix_host_user_page_segment segments[] = {
        {
            .target_address = (unsigned long)reserved,
            .source_page = first,
            .length = linuxPageSize,
            .writable = 0,
            .executable = 0,
        },
        {
            .target_address = (unsigned long)reserved + linuxPageSize,
            .source_page = second,
            .length = linuxPageSize,
            .writable = 0,
            .executable = 0,
        },
    };

    int ret = orlix_host_user_refresh_window((unsigned long)reserved,
                                             (unsigned long)vm_page_size,
                                             segments,
                                             sizeof(segments) / sizeof(segments[0]));
    XCTAssertEqual(ret, 0);

    unsigned char firstByte = ((volatile unsigned char *)reserved)[0];
    unsigned char secondByte = ((volatile unsigned char *)reserved)[linuxPageSize];
    XCTAssertEqual(firstByte, 0x11);
    XCTAssertEqual(secondByte, 0x22);

    orlix_host_user_unmap_pages((unsigned long)reserved,
                                (unsigned long)vm_page_size);
    free(first);
    free(second);
}

- (void)testUserWindowRefreshMapsWritableLinuxPageAfterHoles
{
    if (vm_page_size < 8192) {
        return;
    }

    const unsigned long linuxPageSize = ORLIX_HOST_ADAPTER_TEST_LINUX_PAGE_SIZE;
    vm_address_t reserved = 0;
    kern_return_t status = vm_allocate(mach_task_self(),
                                       &reserved,
                                       (vm_size_t)vm_page_size,
                                       VM_FLAGS_ANYWHERE);
    XCTAssertEqual(status, KERN_SUCCESS);
    XCTAssertNotEqual(reserved, (vm_address_t)0);
    if (status != KERN_SUCCESS || !reserved) {
        return;
    }

    status = vm_deallocate(mach_task_self(), reserved, (vm_size_t)vm_page_size);
    XCTAssertEqual(status, KERN_SUCCESS);

    unsigned char *source = malloc((size_t)linuxPageSize);
    XCTAssertTrue(source != NULL);
    if (!source) {
        return;
    }
    memset(source, 0x33, (size_t)linuxPageSize);

    unsigned long target = (unsigned long)reserved + (unsigned long)vm_page_size - linuxPageSize;
    struct orlix_host_user_page_segment segment = {
        .target_address = target,
        .source_page = source,
        .length = linuxPageSize,
        .writable = 1,
        .executable = 0,
    };

    int ret = orlix_host_user_refresh_window((unsigned long)reserved,
                                             (unsigned long)vm_page_size,
                                             &segment,
                                             1);
    XCTAssertEqual(ret, 0);

    ((volatile unsigned char *)target)[0] = 0x44;
    ((volatile unsigned char *)target)[linuxPageSize - 1] = 0x55;
    orlix_host_user_sync_writable_mappings();
    XCTAssertEqual(source[0], 0x44);
    XCTAssertEqual(source[linuxPageSize - 1], 0x55);

    orlix_host_user_unmap_pages((unsigned long)reserved,
                                (unsigned long)vm_page_size);
    free(source);
}

- (void)testKernelMappingAdaptsMultipleLinuxPagesInsideHostPage
{
    if (vm_page_size < 8192) {
        return;
    }

    const unsigned long linuxPageSize = ORLIX_HOST_ADAPTER_TEST_LINUX_PAGE_SIZE;
    vm_address_t reserved = 0;
    kern_return_t status = vm_allocate(mach_task_self(),
                                       &reserved,
                                       (vm_size_t)vm_page_size,
                                       VM_FLAGS_ANYWHERE);
    XCTAssertEqual(status, KERN_SUCCESS);
    XCTAssertNotEqual(reserved, (vm_address_t)0);
    if (status != KERN_SUCCESS || !reserved) {
        return;
    }

    status = vm_deallocate(mach_task_self(), reserved, (vm_size_t)vm_page_size);
    XCTAssertEqual(status, KERN_SUCCESS);

    unsigned char *first = malloc((size_t)linuxPageSize);
    unsigned char *second = malloc((size_t)linuxPageSize);
    XCTAssertTrue(first != NULL);
    XCTAssertTrue(second != NULL);
    if (!first || !second) {
        free(first);
        free(second);
        return;
    }
    memset(first, 0x61, (size_t)linuxPageSize);
    memset(second, 0x62, (size_t)linuxPageSize);

    unsigned long target = (unsigned long)reserved;
    XCTAssertEqual(orlix_host_kernel_map_page(target,
                                              first,
                                              linuxPageSize),
                   0);
    XCTAssertEqual(orlix_host_kernel_map_page(target + linuxPageSize,
                                              second,
                                              linuxPageSize),
                   0);

    unsigned char firstByte = ((volatile unsigned char *)target)[0];
    unsigned char secondByte = ((volatile unsigned char *)target)[linuxPageSize];
    XCTAssertEqual(firstByte, 0x61);
    XCTAssertEqual(secondByte, 0x62);

    ((volatile unsigned char *)target)[0] = 0x71;
    ((volatile unsigned char *)target)[linuxPageSize] = 0x72;
    orlix_host_kernel_unmap_pages(target, (unsigned long)vm_page_size);
    XCTAssertEqual(first[0], 0x71);
    XCTAssertEqual(second[0], 0x72);

    free(first);
    free(second);
}

- (void)testAppPrivateRootImageFilesSelectReadableBaseAndWritableStateBlocks
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
    ((unsigned char *)base.mutableBytes)[0] = 0xba;
    ((unsigned char *)state.mutableBytes)[0] = 0x5a;
    XCTAssertTrue([base writeToURL:baseURL atomically:YES]);
    XCTAssertTrue([state writeToURL:stateURL atomically:YES]);

    XCTAssertEqual(orlix_host_resources_clear_root_images(), 0);
    XCTAssertEqual(
        orlix_host_resources_register_root_image_files(
            "orlix.env.test",
            "",
            "",
            "initramfs.cpio.gz",
            baseURL.fileSystemRepresentation,
            stateURL.fileSystemRepresentation,
            0,
            1,
            1024),
        0);
    XCTAssertEqual(OrlixHostSelectBootBlockImages("orlix.env.test"), 0);

    unsigned long long sectors = 0;
    XCTAssertEqual(orlix_host_block_capacity(0, &sectors), 0);
    XCTAssertEqual(sectors, 1ULL);
    XCTAssertEqual(orlix_host_block_capacity(1, &sectors), 0);
    XCTAssertEqual(sectors, 2ULL);

    unsigned char buffer[512] = { 0 };
    XCTAssertEqual(orlix_host_block_read(0, 0, buffer, sizeof(buffer)), 0);
    XCTAssertEqual(buffer[0], 0xba);
    memset(buffer, 0, sizeof(buffer));
    XCTAssertEqual(orlix_host_block_read(1, 0, buffer, sizeof(buffer)), 0);
    XCTAssertEqual(buffer[0], 0x5a);

    unsigned char replacement[512] = { 0 };
    replacement[0] = 0xc3;
    XCTAssertNotEqual(orlix_host_block_write(0, 0, replacement, sizeof(replacement)), 0);
    XCTAssertEqual(orlix_host_block_write(1, 1, replacement, sizeof(replacement)), 0);
    memset(buffer, 0, sizeof(buffer));
    XCTAssertEqual(orlix_host_block_read(1, 1, buffer, sizeof(buffer)), 0);
    XCTAssertEqual(buffer[0], 0xc3);

    [fileManager removeItemAtURL:root error:nil];
    XCTAssertEqual(orlix_host_resources_clear_root_images(), 0);
}

- (void)testAppPrivateRootImageFilesRejectRelativeAndParentPaths
{
    XCTAssertEqual(orlix_host_resources_clear_root_images(), 0);
    XCTAssertNotEqual(
        orlix_host_resources_register_root_image_files(
            "orlix.env.bad",
            "",
            "",
            "initramfs.cpio.gz",
            "base.ext4",
            "/tmp/../state.ext4",
            0,
            1,
            1024),
        0);
}

@end

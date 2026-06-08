#import <Foundation/Foundation.h>
#import <XCTest/XCTest.h>

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

@end

#include "OrlixHostAdapter/memory/kernel_mapping.h"

#include <mach/mach.h>
#include <mach/vm_map.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

struct OrlixHostIOMapping {
    void *address;
    unsigned long physical_address;
    vm_size_t length;
    struct OrlixHostIOMapping *next;
};

static struct OrlixHostIOMapping *OrlixHostIOMappings;

static vm_size_t OrlixHostRoundPageLength(unsigned long length)
{
    vm_size_t page_size = (vm_size_t)vm_page_size;
    vm_size_t requested = (vm_size_t)length;

    if (requested == 0 || requested > (vm_size_t)-1 - (page_size - 1)) {
        return 0;
    }

    return (requested + page_size - 1) & ~(page_size - 1);
}

static int OrlixHostMapPageWithProtection(unsigned long target_address,
                                          const void *source_page,
                                          unsigned long length,
                                          vm_prot_t requested_protection)
{
    vm_address_t target = (vm_address_t)target_address;
    vm_prot_t current_protection = VM_PROT_NONE;
    vm_prot_t maximum_protection = VM_PROT_NONE;
    kern_return_t status;

    if (target_address == 0 || !source_page || length == 0) {
        return -1;
    }

    status = vm_remap(mach_task_self(),
                      &target,
                      (vm_size_t)length,
                      0,
                      VM_FLAGS_FIXED | VM_FLAGS_OVERWRITE,
                      mach_task_self(),
                      (vm_address_t)source_page,
                      false,
                      &current_protection,
                      &maximum_protection,
                      VM_INHERIT_NONE);
    if (status != KERN_SUCCESS || target != (vm_address_t)target_address) {
        return -1;
    }

    if (requested_protection != VM_PROT_NONE) {
        status = vm_protect(mach_task_self(),
                            target,
                            (vm_size_t)length,
                            false,
                            requested_protection);
        if (status != KERN_SUCCESS) {
            (void)vm_deallocate(mach_task_self(), target, (vm_size_t)length);
            return -1;
        }
    }

    return 0;
}

static void OrlixHostUnmapPages(unsigned long target_address,
                                unsigned long length)
{
    if (target_address == 0 || length == 0) {
        return;
    }

    (void)vm_deallocate(mach_task_self(),
                        (vm_address_t)target_address,
                        (vm_size_t)length);
}

__attribute__((visibility("hidden"))) int orlix_host_kernel_map_page(
    unsigned long target_address,
    const void *source_page,
    unsigned long length)
{
    return OrlixHostMapPageWithProtection(target_address,
                                          source_page,
                                          length,
                                          VM_PROT_NONE);
}

__attribute__((visibility("hidden"))) void orlix_host_kernel_unmap_pages(
    unsigned long target_address,
    unsigned long length)
{
    OrlixHostUnmapPages(target_address, length);
}

__attribute__((visibility("hidden"))) int orlix_host_user_map_page(
    unsigned long target_address,
    const void *source_page,
    unsigned long length,
    int writable,
    int executable)
{
    vm_prot_t protection = VM_PROT_READ;

    if (writable) {
        protection |= VM_PROT_WRITE;
    }
    if (executable) {
        protection |= VM_PROT_EXECUTE;
    }

    OrlixHostUnmapPages(target_address, length);
    return OrlixHostMapPageWithProtection(target_address,
                                          source_page,
                                          length,
                                          protection);
}

__attribute__((visibility("hidden"))) void orlix_host_user_unmap_pages(
    unsigned long target_address,
    unsigned long length)
{
    OrlixHostUnmapPages(target_address, length);
}

__attribute__((visibility("hidden"))) void *orlix_host_ioremap(
    unsigned long physical_address,
    unsigned long length)
{
    struct OrlixHostIOMapping *mapping;
    vm_address_t mapped = 0;
    vm_size_t rounded_length = OrlixHostRoundPageLength(length);
    kern_return_t status;

    if (physical_address == 0 || rounded_length == 0) {
        return NULL;
    }

    status = vm_allocate(mach_task_self(),
                         &mapped,
                         rounded_length,
                         VM_FLAGS_ANYWHERE);
    if (status != KERN_SUCCESS || mapped == 0) {
        return NULL;
    }

    mapping = malloc(sizeof(*mapping));
    if (!mapping) {
        (void)vm_deallocate(mach_task_self(), mapped, rounded_length);
        return NULL;
    }

    memset((void *)mapped, 0, rounded_length);
    mapping->address = (void *)mapped;
    mapping->physical_address = physical_address;
    mapping->length = rounded_length;
    mapping->next = OrlixHostIOMappings;
    OrlixHostIOMappings = mapping;

    return mapping->address;
}

__attribute__((visibility("hidden"))) void orlix_host_iounmap(
    void *mapped_address)
{
    struct OrlixHostIOMapping **cursor = &OrlixHostIOMappings;

    if (!mapped_address) {
        return;
    }

    while (*cursor) {
        struct OrlixHostIOMapping *mapping = *cursor;

        if (mapping->address == mapped_address) {
            *cursor = mapping->next;
            (void)vm_deallocate(mach_task_self(),
                                (vm_address_t)mapping->address,
                                mapping->length);
            free(mapping);
            return;
        }

        cursor = &mapping->next;
    }
}

__attribute__((visibility("hidden"))) int orlix_host_iomem_physical_address(
    const void *mapped_address,
    unsigned long *physical_address)
{
    const unsigned char *address = mapped_address;
    struct OrlixHostIOMapping *mapping = OrlixHostIOMappings;

    if (!address || !physical_address) {
        return -1;
    }

    while (mapping) {
        const unsigned char *base = mapping->address;
        const unsigned char *end = base + mapping->length;

        if (address >= base && address < end) {
            *physical_address = mapping->physical_address +
                                (unsigned long)(address - base);
            return 0;
        }

        mapping = mapping->next;
    }

    return -1;
}

#include "OrlixHostAdapter/memory/kernel_mapping.h"
#include "OrlixHostAdapter/runtime/host_tls.h"
#include "internal/asm/host_trap.h"

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

struct OrlixHostUserMapping {
    unsigned long target_address;
    const void *source_page;
    vm_size_t length;
    vm_prot_t protection;
    struct OrlixHostUserMapping *next;
};

static struct OrlixHostIOMapping *OrlixHostIOMappings;
static struct OrlixHostUserMapping *OrlixHostUserMappings;

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

static void OrlixHostTranslateLinuxSyscalls(void *target_page,
                                            unsigned long length)
{
    uint32_t *insns = (uint32_t *)target_page;
    unsigned long count = length / sizeof(*insns);

    for (unsigned long index = 0; index < count; index++) {
        if (insns[index] == ORLIX_HOST_AARCH64_SVC0_INSN) {
            insns[index] = ORLIX_HOST_AARCH64_SYSCALL_BRK_INSN;
        }
    }
}

static int OrlixHostMapExecutableUserPage(unsigned long target_address,
                                          const void *source_page,
                                          unsigned long length,
                                          vm_prot_t requested_protection)
{
    vm_address_t target = (vm_address_t)target_address;
    kern_return_t status;

    if (target_address == 0 || !source_page || length == 0) {
        return -1;
    }

    status = vm_allocate(mach_task_self(),
                         &target,
                         (vm_size_t)length,
                         VM_FLAGS_FIXED | VM_FLAGS_OVERWRITE);
    if (status != KERN_SUCCESS || target != (vm_address_t)target_address) {
        return -1;
    }

    memcpy((void *)target, source_page, (size_t)length);
    OrlixHostTranslateLinuxSyscalls((void *)target, length);
    __builtin___clear_cache((char *)target, (char *)(target + length));

    status = vm_protect(mach_task_self(),
                        target,
                        (vm_size_t)length,
                        false,
                        requested_protection);
    if (status != KERN_SUCCESS) {
        (void)vm_deallocate(mach_task_self(), target, (vm_size_t)length);
        return -1;
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

static bool OrlixHostUserMappingMatches(unsigned long target_address,
                                        const void *source_page,
                                        unsigned long length,
                                        vm_prot_t protection)
{
    vm_size_t rounded_length = OrlixHostRoundPageLength(length);

    for (struct OrlixHostUserMapping *mapping = OrlixHostUserMappings;
         mapping;
         mapping = mapping->next) {
        if (mapping->target_address == target_address &&
            mapping->source_page == source_page &&
            mapping->length == rounded_length &&
            mapping->protection == protection) {
            return true;
        }
    }

    return false;
}

static void OrlixHostUserUnmapMappedRange(unsigned long target_address,
                                          unsigned long length)
{
    vm_size_t rounded_length = OrlixHostRoundPageLength(length);
    unsigned long end = target_address + rounded_length;
    struct OrlixHostUserMapping **link = &OrlixHostUserMappings;

    if (target_address == 0 || rounded_length == 0 || end < target_address) {
        return;
    }

    while (*link) {
        struct OrlixHostUserMapping *mapping = *link;
        unsigned long mapping_end = mapping->target_address + mapping->length;

        if (target_address < mapping_end && mapping->target_address < end) {
            (void)vm_deallocate(mach_task_self(),
                                (vm_address_t)mapping->target_address,
                                (vm_size_t)mapping->length);
            *link = mapping->next;
            free(mapping);
            continue;
        }

        link = &mapping->next;
    }
}

static int OrlixHostUserRememberMapping(unsigned long target_address,
                                        const void *source_page,
                                        unsigned long length,
                                        vm_prot_t protection)
{
    vm_size_t rounded_length = OrlixHostRoundPageLength(length);
    struct OrlixHostUserMapping *mapping;

    OrlixHostUserUnmapMappedRange(target_address, length);

    mapping = malloc(sizeof(*mapping));
    if (!mapping) {
        return -1;
    }

    mapping->target_address = target_address;
    mapping->source_page = source_page;
    mapping->length = rounded_length;
    mapping->protection = protection;
    mapping->next = OrlixHostUserMappings;
    OrlixHostUserMappings = mapping;
    return 0;
}

__attribute__((visibility("hidden"))) int orlix_host_kernel_map_page(
    unsigned long target_address,
    const void *source_page,
    unsigned long length)
{
    unsigned long active_tls = OrlixHostEnterHostTls();
    int result = OrlixHostMapPageWithProtection(target_address,
                                                source_page,
                                                length,
                                                VM_PROT_NONE);

    OrlixHostLeaveHostTls(active_tls);
    return result;
}

__attribute__((visibility("hidden"))) void orlix_host_kernel_unmap_pages(
    unsigned long target_address,
    unsigned long length)
{
    unsigned long active_tls = OrlixHostEnterHostTls();

    OrlixHostUnmapPages(target_address, length);
    OrlixHostLeaveHostTls(active_tls);
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

    unsigned long active_tls = OrlixHostEnterHostTls();
    int result;

    if (!executable &&
        OrlixHostUserMappingMatches(target_address,
                                    source_page,
                                    length,
                                    protection)) {
        OrlixHostLeaveHostTls(active_tls);
        return 0;
    }

    OrlixHostUserUnmapMappedRange(target_address, length);
    OrlixHostUnmapPages(target_address, length);
    if (executable) {
        result = OrlixHostMapExecutableUserPage(target_address,
                                               source_page,
                                               length,
                                               protection);
    } else {
        result = OrlixHostMapPageWithProtection(target_address,
                                                source_page,
                                                length,
                                                protection);
    }
    if (result == 0 &&
        OrlixHostUserRememberMapping(target_address,
                                     source_page,
                                     length,
                                     protection) != 0) {
        OrlixHostUnmapPages(target_address, length);
        result = -1;
    }
    OrlixHostLeaveHostTls(active_tls);
    return result;
}

__attribute__((visibility("hidden"))) void orlix_host_user_unmap_pages(
    unsigned long target_address,
    unsigned long length)
{
    unsigned long active_tls = OrlixHostEnterHostTls();

    OrlixHostUserUnmapMappedRange(target_address, length);
    OrlixHostLeaveHostTls(active_tls);
}

__attribute__((visibility("hidden"))) void *orlix_host_ioremap(
    unsigned long physical_address,
    unsigned long length)
{
    struct OrlixHostIOMapping *mapping;
    vm_address_t mapped = 0;
    vm_size_t rounded_length = OrlixHostRoundPageLength(length);
    kern_return_t status;
    unsigned long active_tls;
    void *result = NULL;

    if (physical_address == 0 || rounded_length == 0) {
        return NULL;
    }

    active_tls = OrlixHostEnterHostTls();
    status = vm_allocate(mach_task_self(),
                         &mapped,
                         rounded_length,
                         VM_FLAGS_ANYWHERE);
    if (status != KERN_SUCCESS || mapped == 0) {
        goto out;
    }

    mapping = malloc(sizeof(*mapping));
    if (!mapping) {
        (void)vm_deallocate(mach_task_self(), mapped, rounded_length);
        goto out;
    }

    memset((void *)mapped, 0, rounded_length);
    mapping->address = (void *)mapped;
    mapping->physical_address = physical_address;
    mapping->length = rounded_length;
    mapping->next = OrlixHostIOMappings;
    OrlixHostIOMappings = mapping;
    result = mapping->address;

out:
    OrlixHostLeaveHostTls(active_tls);
    return result;
}

__attribute__((visibility("hidden"))) void orlix_host_iounmap(
    void *mapped_address)
{
    struct OrlixHostIOMapping **cursor = &OrlixHostIOMappings;
    unsigned long active_tls;

    if (!mapped_address) {
        return;
    }

    active_tls = OrlixHostEnterHostTls();
    while (*cursor) {
        struct OrlixHostIOMapping *mapping = *cursor;

        if (mapping->address == mapped_address) {
            *cursor = mapping->next;
            (void)vm_deallocate(mach_task_self(),
                                (vm_address_t)mapping->address,
                                mapping->length);
            free(mapping);
            OrlixHostLeaveHostTls(active_tls);
            return;
        }

        cursor = &mapping->next;
    }

    OrlixHostLeaveHostTls(active_tls);
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

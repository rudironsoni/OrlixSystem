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
    vm_size_t length;
    vm_prot_t protection;
    bool writable;
    struct OrlixHostKernelShadowSegment *segments;
    struct OrlixHostUserMapping *next;
};

struct OrlixHostKernelShadowSegment {
    unsigned long target_address;
    const void *source_page;
    vm_size_t length;
    struct OrlixHostKernelShadowSegment *next;
};

struct OrlixHostKernelShadowMapping {
    unsigned long target_address;
    vm_size_t length;
    struct OrlixHostKernelShadowSegment *segments;
    struct OrlixHostKernelShadowMapping *next;
};

static struct OrlixHostIOMapping *OrlixHostIOMappings;
static struct OrlixHostKernelShadowMapping *OrlixHostKernelShadowMappings;
static struct OrlixHostUserMapping *OrlixHostUserMappings;

__attribute__((visibility("hidden"))) unsigned long orlix_host_memory_page_size(void)
{
    return (unsigned long)vm_page_size;
}

static vm_size_t OrlixHostRoundPageLength(unsigned long length)
{
    vm_size_t page_size = (vm_size_t)orlix_host_memory_page_size();
    vm_size_t requested = (vm_size_t)length;

    if (requested == 0 || requested > (vm_size_t)-1 - (page_size - 1)) {
        return 0;
    }

    return (requested + page_size - 1) & ~(page_size - 1);
}

static void OrlixHostUnmapPages(unsigned long target_address,
                                unsigned long length);
static void OrlixHostUserRemoveSegmentsInRange(
    struct OrlixHostUserMapping *mapping,
    unsigned long target_address,
    unsigned long length,
    bool copy_back_replaced_segments);
static struct OrlixHostUserMapping *OrlixHostUserFindMapping(
    unsigned long target_address,
    unsigned long length,
    vm_prot_t protection,
    bool writable);
static int OrlixHostUserCreateMapping(unsigned long target_address,
                                      unsigned long length,
                                      vm_prot_t protection,
                                      bool writable,
                                      struct OrlixHostUserMapping **out_mapping);

static unsigned long OrlixHostPageStart(unsigned long address)
{
    unsigned long page_size = orlix_host_memory_page_size();

    return address & ~(page_size - 1UL);
}

static unsigned long OrlixHostPageEnd(unsigned long address,
                                      unsigned long length)
{
    unsigned long page_size = orlix_host_memory_page_size();
    unsigned long end;

    if (length == 0 || address > (unsigned long)-1 - length) {
        return 0;
    }

    end = address + length;
    if (end > (unsigned long)-1 - (page_size - 1UL)) {
        return 0;
    }

    return (end + page_size - 1UL) & ~(page_size - 1UL);
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

static void OrlixHostKernelCopyBackMapping(
    struct OrlixHostKernelShadowMapping *mapping)
{
    if (!mapping) {
        return;
    }

    for (struct OrlixHostKernelShadowSegment *segment = mapping->segments;
         segment;
         segment = segment->next) {
        if (!segment->source_page || segment->length == 0) {
            continue;
        }

        memcpy((void *)segment->source_page,
               (const void *)segment->target_address,
               (size_t)segment->length);
    }
}

static void OrlixHostKernelFreeShadowSegments(
    struct OrlixHostKernelShadowMapping *mapping)
{
    if (!mapping) {
        return;
    }

    while (mapping->segments) {
        struct OrlixHostKernelShadowSegment *segment = mapping->segments;

        mapping->segments = segment->next;
        free(segment);
    }
}

static void OrlixHostKernelRemoveShadowSegmentsInRange(
    struct OrlixHostKernelShadowMapping *mapping,
    unsigned long target_address,
    unsigned long length)
{
    unsigned long end;
    struct OrlixHostKernelShadowSegment **link;

    if (!mapping || target_address == 0 || length == 0 ||
        target_address > (unsigned long)-1 - length) {
        return;
    }

    end = target_address + length;
    link = &mapping->segments;
    while (*link) {
        struct OrlixHostKernelShadowSegment *segment = *link;
        unsigned long segment_end = segment->target_address + segment->length;

        if (target_address < segment_end && segment->target_address < end) {
            memcpy((void *)segment->source_page,
                   (const void *)segment->target_address,
                   (size_t)segment->length);
            *link = segment->next;
            free(segment);
            continue;
        }

        link = &segment->next;
    }
}

static void OrlixHostKernelUnmapShadowRange(unsigned long target_address,
                                            unsigned long length)
{
    unsigned long start = OrlixHostPageStart(target_address);
    unsigned long end = OrlixHostPageEnd(target_address, length);
    struct OrlixHostKernelShadowMapping **link =
        &OrlixHostKernelShadowMappings;

    if (target_address == 0 || end == 0 || end <= start) {
        return;
    }

    while (*link) {
        struct OrlixHostKernelShadowMapping *mapping = *link;
        unsigned long mapping_end =
            mapping->target_address + mapping->length;

        if (start < mapping_end && mapping->target_address < end) {
            OrlixHostKernelCopyBackMapping(mapping);
            OrlixHostKernelFreeShadowSegments(mapping);
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

static struct OrlixHostKernelShadowMapping *
OrlixHostKernelFindShadowMapping(unsigned long target_address,
                                 unsigned long length)
{
    unsigned long start = OrlixHostPageStart(target_address);
    unsigned long end = OrlixHostPageEnd(target_address, length);

    if (target_address == 0 || end == 0 || end <= start) {
        return NULL;
    }

    for (struct OrlixHostKernelShadowMapping *mapping =
             OrlixHostKernelShadowMappings;
         mapping;
         mapping = mapping->next) {
        unsigned long mapping_end =
            mapping->target_address + mapping->length;

        if (mapping->target_address <= target_address &&
            mapping_end >= target_address + length) {
            return mapping;
        }
    }

    return NULL;
}

static int OrlixHostKernelCreateShadowMapping(unsigned long target_address,
                                              unsigned long length,
                                              struct OrlixHostKernelShadowMapping **out_mapping)
{
    unsigned long start = OrlixHostPageStart(target_address);
    unsigned long end = OrlixHostPageEnd(target_address, length);
    vm_address_t target = (vm_address_t)start;
    struct OrlixHostKernelShadowMapping *mapping;
    kern_return_t status;

    if (!out_mapping || target_address == 0 || end == 0 || end <= start) {
        return -1;
    }

    OrlixHostKernelUnmapShadowRange(start, end - start);
    OrlixHostUnmapPages(start, end - start);

    status = vm_allocate(mach_task_self(),
                         &target,
                         (vm_size_t)(end - start),
                         VM_FLAGS_FIXED | VM_FLAGS_OVERWRITE);
    if (status != KERN_SUCCESS || target != (vm_address_t)start) {
        return -1;
    }

    mapping = malloc(sizeof(*mapping));
    if (!mapping) {
        (void)vm_deallocate(mach_task_self(), target, (vm_size_t)(end - start));
        return -1;
    }

    mapping->target_address = start;
    mapping->length = (vm_size_t)(end - start);
    mapping->segments = NULL;
    mapping->next = OrlixHostKernelShadowMappings;
    OrlixHostKernelShadowMappings = mapping;
    *out_mapping = mapping;
    return 0;
}

static int OrlixHostMapShadowKernelPage(unsigned long target_address,
                                        const void *source_page,
                                        unsigned long length)
{
    struct OrlixHostKernelShadowMapping *mapping;
    struct OrlixHostKernelShadowSegment *segment;
    unsigned long offset;

    if (target_address == 0 || !source_page || length == 0 ||
        target_address > (unsigned long)-1 - length) {
        return -1;
    }

    mapping = OrlixHostKernelFindShadowMapping(target_address, length);
    if (!mapping &&
        OrlixHostKernelCreateShadowMapping(target_address,
                                           length,
                                           &mapping) != 0) {
        return -1;
    }
    if (!mapping) {
        return -1;
    }

    offset = target_address - mapping->target_address;
    OrlixHostKernelRemoveShadowSegmentsInRange(mapping,
                                               target_address,
                                               length);
    memcpy((void *)(mapping->target_address + offset),
           source_page,
           (size_t)length);

    segment = malloc(sizeof(*segment));
    if (!segment) {
        return -1;
    }
    segment->target_address = target_address;
    segment->source_page = source_page;
    segment->length = (vm_size_t)length;
    segment->next = mapping->segments;
    mapping->segments = segment;
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
        } else if ((insns[index] & ORLIX_HOST_AARCH64_MSR_TPIDR_EL0_MASK) ==
                   ORLIX_HOST_AARCH64_MSR_TPIDR_EL0) {
            insns[index] = ORLIX_HOST_AARCH64_TLS_WRITE_BRK_BASE |
                           (insns[index] & 0x1fU);
        }
    }
}

static int OrlixHostMapShadowUserPages(unsigned long target_address,
                                       const void *source_page,
                                       unsigned long length,
                                       vm_prot_t requested_protection,
                                       bool executable,
                                       bool translate_executable,
                                       bool copy_back_replaced_segments)
{
    struct OrlixHostUserMapping *mapping;
    struct OrlixHostKernelShadowSegment *segment;
    unsigned long offset;
    vm_prot_t copy_protection = VM_PROT_READ | VM_PROT_WRITE;
    kern_return_t status;

    if (target_address == 0 || !source_page || length == 0 ||
        target_address > (unsigned long)-1 - length) {
        return -1;
    }

    mapping = OrlixHostUserFindMapping(target_address,
                                       length,
                                       requested_protection,
                                       requested_protection & VM_PROT_WRITE);
    if (!mapping &&
        OrlixHostUserCreateMapping(target_address,
                                   length,
                                   requested_protection,
                                   requested_protection & VM_PROT_WRITE,
                                   &mapping) != 0) {
        return -1;
    }
    if (!mapping) {
        return -1;
    }

    status = vm_protect(mach_task_self(),
                        (vm_address_t)mapping->target_address,
                        mapping->length,
                        false,
                        copy_protection);
    if (status != KERN_SUCCESS) {
        return -1;
    }

    offset = target_address - mapping->target_address;
    OrlixHostUserRemoveSegmentsInRange(mapping,
                                       target_address,
                                       length,
                                       copy_back_replaced_segments);
    memcpy((void *)(mapping->target_address + offset),
           source_page,
           (size_t)length);
    if (executable && translate_executable) {
        OrlixHostTranslateLinuxSyscalls((void *)(mapping->target_address + offset),
                                        length);
    }
    if (executable) {
        __builtin___clear_cache((char *)(mapping->target_address + offset),
                                (char *)(mapping->target_address + offset + length));
    }

    segment = malloc(sizeof(*segment));
    if (!segment) {
        return -1;
    }
    segment->target_address = target_address;
    segment->source_page = source_page;
    segment->length = (vm_size_t)length;
    segment->next = mapping->segments;
    mapping->segments = segment;

    status = vm_protect(mach_task_self(),
                        (vm_address_t)mapping->target_address,
                        mapping->length,
                        false,
                        requested_protection);
    if (status != KERN_SUCCESS) {
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
                                        vm_prot_t protection,
                                        bool writable)
{
    for (struct OrlixHostUserMapping *mapping = OrlixHostUserMappings;
         mapping;
         mapping = mapping->next) {
        unsigned long mapping_end = mapping->target_address + mapping->length;

        if (mapping->target_address <= target_address &&
            mapping_end >= target_address + length &&
            mapping->protection == protection &&
            mapping->writable == writable) {
            for (struct OrlixHostKernelShadowSegment *segment =
                     mapping->segments;
                 segment;
                 segment = segment->next) {
                if (segment->target_address == target_address &&
                    segment->source_page == source_page &&
                    segment->length == length) {
                    return true;
                }
            }
        }
    }

    return false;
}

static void OrlixHostUserCopyBackMapping(struct OrlixHostUserMapping *mapping)
{
    if (!mapping || !mapping->writable) {
        return;
    }

    for (struct OrlixHostKernelShadowSegment *segment = mapping->segments;
         segment;
         segment = segment->next) {
        if (!segment->source_page || segment->length == 0) {
            continue;
        }

        memcpy((void *)segment->source_page,
               (const void *)segment->target_address,
               (size_t)segment->length);
    }
}

static void OrlixHostUserFreeSegments(struct OrlixHostUserMapping *mapping)
{
    if (!mapping) {
        return;
    }

    while (mapping->segments) {
        struct OrlixHostKernelShadowSegment *segment = mapping->segments;

        mapping->segments = segment->next;
        free(segment);
    }
}

static void OrlixHostUserRemoveSegmentsInRange(
    struct OrlixHostUserMapping *mapping,
    unsigned long target_address,
    unsigned long length,
    bool copy_back_replaced_segments)
{
    unsigned long end;
    struct OrlixHostKernelShadowSegment **link;

    if (!mapping || target_address == 0 || length == 0 ||
        target_address > (unsigned long)-1 - length) {
        return;
    }

    end = target_address + length;
    link = &mapping->segments;
    while (*link) {
        struct OrlixHostKernelShadowSegment *segment = *link;
        unsigned long segment_end = segment->target_address + segment->length;

        if (target_address < segment_end && segment->target_address < end) {
            if (copy_back_replaced_segments &&
                mapping->writable &&
                segment->source_page) {
                memcpy((void *)segment->source_page,
                       (const void *)segment->target_address,
                       (size_t)segment->length);
            }
            *link = segment->next;
            free(segment);
            continue;
        }

        link = &segment->next;
    }
}

static void OrlixHostUserUnmapMappedRange(unsigned long target_address,
                                          unsigned long length)
{
    unsigned long start = OrlixHostPageStart(target_address);
    unsigned long end = OrlixHostPageEnd(target_address, length);
    struct OrlixHostUserMapping **link = &OrlixHostUserMappings;

    if (target_address == 0 || end == 0 || end <= start) {
        return;
    }

    while (*link) {
        struct OrlixHostUserMapping *mapping = *link;
        unsigned long mapping_end = mapping->target_address + mapping->length;

        if (start < mapping_end && mapping->target_address < end) {
            OrlixHostUserCopyBackMapping(mapping);
            OrlixHostUserFreeSegments(mapping);
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

static struct OrlixHostUserMapping *OrlixHostUserFindMapping(
    unsigned long target_address,
    unsigned long length,
    vm_prot_t protection,
    bool writable)
{
    for (struct OrlixHostUserMapping *mapping = OrlixHostUserMappings;
         mapping;
         mapping = mapping->next) {
        unsigned long mapping_end = mapping->target_address + mapping->length;

        if (mapping->target_address <= target_address &&
            mapping_end >= target_address + length &&
            mapping->protection == protection &&
            mapping->writable == writable) {
            return mapping;
        }
    }

    return NULL;
}

static int OrlixHostUserCreateMapping(unsigned long target_address,
                                      unsigned long length,
                                      vm_prot_t protection,
                                      bool writable,
                                      struct OrlixHostUserMapping **out_mapping)
{
    unsigned long start = OrlixHostPageStart(target_address);
    unsigned long end = OrlixHostPageEnd(target_address, length);
    vm_address_t target = (vm_address_t)start;
    struct OrlixHostUserMapping *mapping;
    kern_return_t status;

    if (!out_mapping || target_address == 0 || end == 0 || end <= start) {
        return -1;
    }

    OrlixHostUserUnmapMappedRange(start, end - start);
    OrlixHostUnmapPages(start, end - start);

    status = vm_allocate(mach_task_self(),
                         &target,
                         (vm_size_t)(end - start),
                         VM_FLAGS_FIXED | VM_FLAGS_OVERWRITE);
    if (status != KERN_SUCCESS || target != (vm_address_t)start) {
        return -1;
    }

    mapping = malloc(sizeof(*mapping));
    if (!mapping) {
        (void)vm_deallocate(mach_task_self(), target, (vm_size_t)(end - start));
        return -1;
    }

    mapping->target_address = start;
    mapping->length = (vm_size_t)(end - start);
    mapping->protection = protection;
    mapping->writable = writable;
    mapping->segments = NULL;
    mapping->next = OrlixHostUserMappings;
    OrlixHostUserMappings = mapping;
    *out_mapping = mapping;
    return 0;
}

__attribute__((visibility("hidden"))) int orlix_host_kernel_map_page(
    unsigned long target_address,
    const void *source_page,
    unsigned long length)
{
    unsigned long active_tls = OrlixHostEnterHostTls();
    int result;

    if (OrlixHostKernelFindShadowMapping(target_address, length)) {
        result = OrlixHostMapShadowKernelPage(target_address,
                                             source_page,
                                             length);
    } else {
        result = OrlixHostMapPageWithProtection(target_address,
                                                source_page,
                                                length,
                                                VM_PROT_NONE);
        if (result != 0) {
            result = OrlixHostMapShadowKernelPage(target_address,
                                                 source_page,
                                                 length);
        }
    }

    OrlixHostLeaveHostTls(active_tls);
    return result;
}

__attribute__((visibility("hidden"))) void orlix_host_kernel_unmap_pages(
    unsigned long target_address,
    unsigned long length)
{
    unsigned long active_tls = OrlixHostEnterHostTls();

    OrlixHostKernelUnmapShadowRange(target_address, length);
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
                                    protection,
                                    writable != 0)) {
        OrlixHostLeaveHostTls(active_tls);
        return 0;
    }

    result = OrlixHostMapShadowUserPages(target_address,
                                         source_page,
                                         length,
                                         protection,
                                         executable != 0,
                                         true,
                                         true);
    OrlixHostLeaveHostTls(active_tls);
    return result;
}

__attribute__((visibility("hidden"))) int orlix_host_user_map_trusted_executable_page(
    unsigned long target_address,
    const void *source_page,
    unsigned long length)
{
    unsigned long active_tls = OrlixHostEnterHostTls();
    int result = OrlixHostMapShadowUserPages(target_address,
                                             source_page,
                                             length,
                                             VM_PROT_READ | VM_PROT_EXECUTE,
                                             true,
                                             false,
                                             true);
    OrlixHostLeaveHostTls(active_tls);
    return result;
}

__attribute__((visibility("hidden"))) int orlix_host_user_refresh_page(
    unsigned long target_address,
    const void *source_page,
    unsigned long length,
    int writable,
    int executable)
{
    vm_prot_t protection = VM_PROT_READ;
    unsigned long active_tls;
    int result;

    if (writable) {
        protection |= VM_PROT_WRITE;
    }
    if (executable) {
        protection |= VM_PROT_EXECUTE;
    }

    active_tls = OrlixHostEnterHostTls();
    result = OrlixHostMapShadowUserPages(target_address,
                                         source_page,
                                         length,
                                         protection,
                                         executable != 0,
                                         true,
                                         false);
    OrlixHostLeaveHostTls(active_tls);
    return result;
}

__attribute__((visibility("hidden"))) int orlix_host_user_refresh_window(
    unsigned long target_address,
    unsigned long length,
    const struct orlix_host_user_page_segment *segments,
    unsigned long segment_count)
{
    vm_prot_t protection = VM_PROT_READ;
    vm_prot_t copy_protection = VM_PROT_READ | VM_PROT_WRITE;
    unsigned long end;
    unsigned long active_tls;
    struct OrlixHostUserMapping *mapping;
    kern_return_t status;
    bool writable = false;
    bool executable = false;

    if (target_address == 0 || length == 0 || !segments ||
        segment_count == 0 || target_address > (unsigned long)-1 - length) {
        return -1;
    }

    end = target_address + length;
    for (unsigned long index = 0; index < segment_count; index++) {
        const struct orlix_host_user_page_segment *segment = &segments[index];
        unsigned long segment_end;

        if (!segment->source_page || segment->length == 0 ||
            segment->target_address < target_address ||
            segment->target_address > (unsigned long)-1 - segment->length) {
            return -1;
        }
        segment_end = segment->target_address + segment->length;
        if (segment_end > end) {
            return -1;
        }
        if (segment->writable) {
            writable = true;
        }
        if (segment->executable) {
            executable = true;
        }
    }

    if (writable) {
        protection |= VM_PROT_WRITE;
    }
    if (executable) {
        protection |= VM_PROT_EXECUTE;
    }

    active_tls = OrlixHostEnterHostTls();
    mapping = OrlixHostUserFindMapping(target_address,
                                       length,
                                       protection,
                                       writable);
    if (!mapping &&
        OrlixHostUserCreateMapping(target_address,
                                   length,
                                   protection,
                                   writable,
                                   &mapping) != 0) {
        OrlixHostLeaveHostTls(active_tls);
        return -1;
    }
    if (!mapping) {
        OrlixHostLeaveHostTls(active_tls);
        return -1;
    }

    status = vm_protect(mach_task_self(),
                        (vm_address_t)mapping->target_address,
                        mapping->length,
                        false,
                        copy_protection);
    if (status != KERN_SUCCESS) {
        OrlixHostLeaveHostTls(active_tls);
        return -1;
    }

    OrlixHostUserRemoveSegmentsInRange(mapping,
                                       target_address,
                                       length,
                                       false);
    for (unsigned long index = 0; index < segment_count; index++) {
        const struct orlix_host_user_page_segment *input = &segments[index];
        struct OrlixHostKernelShadowSegment *segment;
        unsigned long offset = input->target_address - mapping->target_address;

        memcpy((void *)(mapping->target_address + offset),
               input->source_page,
               (size_t)input->length);
        if (input->executable) {
            OrlixHostTranslateLinuxSyscalls(
                (void *)(mapping->target_address + offset),
                input->length);
            __builtin___clear_cache(
                (char *)(mapping->target_address + offset),
                (char *)(mapping->target_address + offset + input->length));
        }

        segment = malloc(sizeof(*segment));
        if (!segment) {
            OrlixHostUserUnmapMappedRange(mapping->target_address,
                                          mapping->length);
            OrlixHostLeaveHostTls(active_tls);
            return -1;
        }
        segment->target_address = input->target_address;
        segment->source_page = input->source_page;
        segment->length = (vm_size_t)input->length;
        segment->next = mapping->segments;
        mapping->segments = segment;
    }

    status = vm_protect(mach_task_self(),
                        (vm_address_t)mapping->target_address,
                        mapping->length,
                        false,
                        protection);
    if (status != KERN_SUCCESS) {
        OrlixHostUserUnmapMappedRange(mapping->target_address,
                                      mapping->length);
        OrlixHostLeaveHostTls(active_tls);
        return -1;
    }

    OrlixHostLeaveHostTls(active_tls);
    return 0;
}

__attribute__((visibility("hidden"))) void orlix_host_user_unmap_pages(
    unsigned long target_address,
    unsigned long length)
{
    unsigned long active_tls = OrlixHostEnterHostTls();

    OrlixHostUserUnmapMappedRange(target_address, length);
    OrlixHostUnmapPages(target_address, length);
    OrlixHostLeaveHostTls(active_tls);
}

__attribute__((visibility("hidden"))) void orlix_host_user_sync_writable_mappings(void)
{
    unsigned long active_tls = OrlixHostEnterHostTls();

    for (struct OrlixHostUserMapping *mapping = OrlixHostUserMappings;
         mapping;
         mapping = mapping->next) {
        OrlixHostUserCopyBackMapping(mapping);
    }

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

#ifndef ORLIX_HOST_ADAPTER_MEMORY_KERNEL_MAPPING_H
#define ORLIX_HOST_ADAPTER_MEMORY_KERNEL_MAPPING_H

__attribute__((visibility("hidden"))) int orlix_host_kernel_map_page(
    unsigned long target_address,
    const void *source_page,
    unsigned long length);

__attribute__((visibility("hidden"))) void orlix_host_kernel_unmap_pages(
    unsigned long target_address,
    unsigned long length);

__attribute__((visibility("hidden"))) void *orlix_host_ioremap(
    unsigned long physical_address,
    unsigned long length);

__attribute__((visibility("hidden"))) void orlix_host_iounmap(
    void *mapped_address);

#endif

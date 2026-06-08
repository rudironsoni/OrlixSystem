#ifndef ORLIX_HOST_ADAPTER_MEMORY_KERNEL_MAPPING_H
#define ORLIX_HOST_ADAPTER_MEMORY_KERNEL_MAPPING_H

struct orlix_host_user_page_segment {
    unsigned long target_address;
    const void *source_page;
    unsigned long length;
    int writable;
    int executable;
};

__attribute__((visibility("hidden"))) unsigned long orlix_host_memory_page_size(void);

__attribute__((visibility("hidden"))) int orlix_host_kernel_map_page(
    unsigned long target_address,
    const void *source_page,
    unsigned long length);

__attribute__((visibility("hidden"))) void orlix_host_kernel_unmap_pages(
    unsigned long target_address,
    unsigned long length);

__attribute__((visibility("hidden"))) int orlix_host_user_map_page(
    unsigned long target_address,
    const void *source_page,
    unsigned long length,
    int writable,
    int executable);

__attribute__((visibility("hidden"))) int orlix_host_user_map_trusted_executable_page(
    unsigned long target_address,
    const void *source_page,
    unsigned long length);

__attribute__((visibility("hidden"))) int orlix_host_user_refresh_page(
    unsigned long target_address,
    const void *source_page,
    unsigned long length,
    int writable,
    int executable);

__attribute__((visibility("hidden"))) int orlix_host_user_refresh_window(
    unsigned long target_address,
    unsigned long length,
    const struct orlix_host_user_page_segment *segments,
    unsigned long segment_count);

__attribute__((visibility("hidden"))) void orlix_host_user_unmap_pages(
    unsigned long target_address,
    unsigned long length);

__attribute__((visibility("hidden"))) void orlix_host_user_sync_writable_mappings(void);

__attribute__((visibility("hidden"))) void *orlix_host_ioremap(
    unsigned long physical_address,
    unsigned long length);

__attribute__((visibility("hidden"))) void orlix_host_iounmap(
    void *mapped_address);

__attribute__((visibility("hidden"))) int orlix_host_iomem_physical_address(
    const void *mapped_address,
    unsigned long *physical_address);

#endif

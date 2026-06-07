#ifndef ORLIX_HOST_BOOT_RESOURCES_H
#define ORLIX_HOST_BOOT_RESOURCES_H

struct OrlixHostResource {
    void *data;
    unsigned long size;
};

/* App-private HostAdapter SPI; OrlixOS resolves its own target bundle. */
__attribute__((visibility("default"))) int orlix_host_resources_set_payload_root_path(
    const char *path);

__attribute__((visibility("default"))) int orlix_host_resources_clear_root_images(void);

__attribute__((visibility("default"))) int orlix_host_resources_register_root_image(
    const char *identifier,
    const char *initrd_bundle_name,
    const char *initrd_bundle_extension,
    const char *initrd_resource,
    const char *base_block_resource,
    const char *state_block_resource,
    unsigned int base_block_device,
    unsigned int state_block_device,
    unsigned long long state_block_minimum_bytes);

__attribute__((visibility("hidden"))) int OrlixHostLoadKernelPayloadResource(
    const char *resource,
    struct OrlixHostResource *loaded);

__attribute__((visibility("hidden"))) int OrlixHostLoadInitrdResource(
    const char *identifier,
    struct OrlixHostResource *loaded);

__attribute__((visibility("hidden"))) int OrlixHostSelectBootBlockImages(
    const char *identifier);

__attribute__((visibility("hidden"))) int orlix_host_block_capacity(
    unsigned int device,
    unsigned long long *sectors);

__attribute__((visibility("hidden"))) int orlix_host_block_read(
    unsigned int device,
    unsigned long long sector,
    void *buffer,
    unsigned int length);

__attribute__((visibility("hidden"))) int orlix_host_block_write(
    unsigned int device,
    unsigned long long sector,
    const void *buffer,
    unsigned int length);

__attribute__((visibility("hidden"))) void OrlixHostFreeResource(
    struct OrlixHostResource *resource);

#endif

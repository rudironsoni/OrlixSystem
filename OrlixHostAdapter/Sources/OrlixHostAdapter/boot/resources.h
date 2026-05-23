#ifndef ORLIX_HOST_BOOT_RESOURCES_H
#define ORLIX_HOST_BOOT_RESOURCES_H

struct OrlixHostResource {
    void *data;
    unsigned long size;
};

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

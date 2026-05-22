#ifndef ORLIX_HOST_BOOT_RESOURCES_H
#define ORLIX_HOST_BOOT_RESOURCES_H

struct OrlixHostResource {
    void *data;
    unsigned long size;
};

__attribute__((visibility("hidden"))) int OrlixHostLoadKernelPayloadResource(
    const char *resource,
    struct OrlixHostResource *loaded);

__attribute__((visibility("hidden"))) int OrlixHostLoadRootImageResource(
    const char *identifier,
    struct OrlixHostResource *loaded);

__attribute__((visibility("hidden"))) void OrlixHostFreeResource(
    struct OrlixHostResource *resource);

#endif

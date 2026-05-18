#ifndef ORLIX_BOOT_PAYLOAD_H
#define ORLIX_BOOT_PAYLOAD_H

__attribute__((visibility("hidden"))) int OrlixLoadKernelImage(
    const void *image,
    unsigned long image_size);

__attribute__((visibility("hidden"))) int OrlixLoadDeviceTree(
    const void *image,
    unsigned long image_size);

__attribute__((visibility("hidden"))) int OrlixLoadInitrd(
    const void *image,
    unsigned long image_size);

__attribute__((visibility("hidden"))) int OrlixSelectRootImage(
    const char *identifier);

#endif

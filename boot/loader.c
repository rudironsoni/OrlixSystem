#include "OrlixKernel.h"

__attribute__((visibility("hidden"))) int OrlixLoadKernelImage(const void *image,
                                                               unsigned long image_size);
__attribute__((visibility("hidden"))) int OrlixLoadInitrd(const void *image,
                                                          unsigned long image_size);
__attribute__((visibility("hidden"))) int OrlixSelectRootImage(const char *device);
__attribute__((visibility("hidden"))) int OrlixLoadDeviceTree(const void *image,
                                                              unsigned long image_size);
__attribute__((visibility("hidden"))) int OrlixPrepareBootParams(struct boot_params *params);
__attribute__((visibility("hidden"))) void arch_boot_entry(const struct boot_params *params);

int OrlixBoot(const struct boot_params *params) {
    if (!params) {
        return -1;
    }
    if (OrlixPrepareBootParams((struct boot_params *)params) != 0) {
        return -1;
    }
    if (OrlixLoadKernelImage((const void *)params->memory_base, params->memory_size) != 0) {
        return -1;
    }
    if (OrlixLoadInitrd(params->initrd_base, params->initrd_size) != 0) {
        return -1;
    }
    if (OrlixSelectRootImage(params->root_device) != 0) {
        return -1;
    }
    if (OrlixLoadDeviceTree(params->dtb_base, params->dtb_size) != 0) {
        return -1;
    }
    arch_boot_entry(params);
    return 0;
}

#include "OrlixKernel.h"

__attribute__((visibility("hidden"))) int OrlixLoadKernelImage(const void *image,
                                                               unsigned long image_size);
__attribute__((visibility("hidden"))) int OrlixLoadInitrd(const void *image,
                                                          unsigned long image_size);
__attribute__((visibility("hidden"))) int OrlixSelectRootImage(const char *path);
__attribute__((visibility("hidden"))) int OrlixLoadDeviceTree(const void *image,
                                                              unsigned long image_size);

int OrlixBoot(const struct boot_params *params) {
    if (!params) {
        return -1;
    }
    if (OrlixPrepareBootParams((struct boot_params *)params) != 0) {
        return -1;
    }
    if (OrlixLoadKernelImage(params->kernel_image, params->kernel_image_size) != 0) {
        return -1;
    }
    if (OrlixLoadInitrd(params->initrd_image, params->initrd_size) != 0) {
        return -1;
    }
    if (OrlixSelectRootImage(params->root_image_path) != 0) {
        return -1;
    }
    if (OrlixLoadDeviceTree(params->device_tree, params->device_tree_size) != 0) {
        return -1;
    }
    return 0;
}

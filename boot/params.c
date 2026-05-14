#include "OrlixKernel.h"

int OrlixPrepareBootParams(struct boot_params *params) {
    if (!params) {
        return -1;
    }
    if (!params->kernel_image || params->kernel_image_size == 0) {
        return -1;
    }
    if (!params->initrd_image || params->initrd_size == 0) {
        return -1;
    }
    if (!params->root_image_path || params->root_image_path[0] == '\0') {
        return -1;
    }
    if (!params->device_tree || params->device_tree_size == 0) {
        return -1;
    }
    return 0;
}

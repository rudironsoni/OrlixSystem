#include "OrlixKernel.h"

__attribute__((visibility("hidden"))) int OrlixPrepareBootParams(struct boot_params *params) {
    if (!params) {
        return -1;
    }
    if (!params->cmdline || params->cmdline[0] == '\0') {
        return -1;
    }
    if (params->memory_base == 0 || params->memory_size == 0) {
        return -1;
    }
    if (!params->initrd_base || params->initrd_size == 0) {
        return -1;
    }
    if (!params->dtb_base || params->dtb_size == 0) {
        return -1;
    }
    if (!params->root_device || params->root_device[0] == '\0') {
        return -1;
    }
    if (!params->console_device || params->console_device[0] == '\0') {
        return -1;
    }
    return 0;
}

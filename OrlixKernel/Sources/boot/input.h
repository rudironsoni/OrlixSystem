#ifndef ORLIX_BOOT_INPUT_H
#define ORLIX_BOOT_INPUT_H

#include "OrlixKernel.h"

struct OrlixBootInput {
    enum OrlixBootProfile profile;
    const char *profile_dtb_path;
    const char *kernel_cmdline;
    const char *root_device;
    const char *console_device;
    const char *root_image_identifier;
    const char *terminal_identifier;
};

__attribute__((visibility("hidden"))) int OrlixPrepareBootInput(
    const struct OrlixBootConfig *config,
    struct OrlixBootInput *input);

#endif

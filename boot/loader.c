#include "OrlixKernel.h"

__attribute__((visibility("hidden"))) int OrlixPrepareBootConfig(const struct OrlixBootConfig *config);

int OrlixBoot(const struct OrlixBootConfig *config) {
    if (OrlixPrepareBootConfig(config) != ORLIX_BOOT_STATUS_OK) {
        return ORLIX_BOOT_STATUS_INVALID_CONFIG;
    }

    return ORLIX_BOOT_STATUS_UNAVAILABLE;
}

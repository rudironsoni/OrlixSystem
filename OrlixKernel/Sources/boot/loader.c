#include "boot/handoff.h"

int OrlixBoot(const struct OrlixBootConfig *config) {
    struct OrlixBootInput input;

    if (OrlixPrepareBootInput(config, &input) != ORLIX_BOOT_STATUS_OK) {
        return ORLIX_BOOT_STATUS_INVALID_CONFIG;
    }

    return OrlixBootHandoff(&input);
}

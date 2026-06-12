#include "boot/handoff.h"

static int OrlixBootStarted;

int OrlixBoot(const struct OrlixBootConfig *config) {
    struct OrlixBootInput input;
    int status;

    if (OrlixPrepareBootInput(config, &input) != ORLIX_BOOT_STATUS_OK) {
        return ORLIX_BOOT_STATUS_INVALID_CONFIG;
    }

    if (!__sync_bool_compare_and_swap(&OrlixBootStarted, 0, 1)) {
        return ORLIX_BOOT_STATUS_ALREADY_STARTED;
    }

    status = OrlixBootHandoff(&input);
    if (status == ORLIX_BOOT_STATUS_INVALID_CONFIG) {
        (void)__sync_bool_compare_and_swap(&OrlixBootStarted, 1, 0);
    }
    return status;
}

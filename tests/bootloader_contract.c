#include "OrlixKernel.h"

int OrlixPrepareBootConfig(const struct OrlixBootConfig *config);

static int expect_invalid_config(const struct OrlixBootConfig *config)
{
    return OrlixPrepareBootConfig(config) == ORLIX_BOOT_STATUS_INVALID_CONFIG ? 0 : -1;
}

int main(void)
{
    struct OrlixBootConfig config = {
        .profile = ORLIX_BOOT_PROFILE_APPSTORE,
        .root_image_identifier = "default-root",
        .terminal_identifier = "default-terminal",
    };

    if (expect_invalid_config(0) != 0) {
        return 1;
    }

    config.profile = (enum OrlixBootProfile)99;
    if (expect_invalid_config(&config) != 0) {
        return 2;
    }
    config.profile = ORLIX_BOOT_PROFILE_APPSTORE;

    config.root_image_identifier = 0;
    if (expect_invalid_config(&config) != 0) {
        return 3;
    }
    config.root_image_identifier = "default-root";

    config.root_image_identifier = "";
    if (expect_invalid_config(&config) != 0) {
        return 4;
    }
    config.root_image_identifier = "default-root";

    config.terminal_identifier = 0;
    if (expect_invalid_config(&config) != 0) {
        return 5;
    }
    config.terminal_identifier = "default-terminal";

    config.terminal_identifier = "";
    if (expect_invalid_config(&config) != 0) {
        return 6;
    }
    config.terminal_identifier = "default-terminal";

    if (OrlixPrepareBootConfig(&config) != ORLIX_BOOT_STATUS_OK) {
        return 7;
    }

    if (OrlixBoot(&config) != ORLIX_BOOT_STATUS_UNAVAILABLE) {
        return 8;
    }

    return 0;
}

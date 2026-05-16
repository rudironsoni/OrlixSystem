#include <string.h>

#include "OrlixKernel.h"
#include "boot/input.h"

int OrlixPrepareBootConfig(const struct OrlixBootConfig *config);

static int expect_invalid_config(const struct OrlixBootConfig *config)
{
    return OrlixBoot(config) == ORLIX_BOOT_STATUS_INVALID_CONFIG ? 0 : -1;
}

static int expect_string(const char *actual, const char *expected)
{
    if (!actual || !expected) {
        return -1;
    }
    return strcmp(actual, expected) == 0 ? 0 : -1;
}

static int expect_profile_input(enum OrlixBootProfile profile,
                                const char *expected_dtb,
                                const char *expected_console,
                                const char *expected_root,
                                const char *expected_cmdline)
{
    struct OrlixBootConfig config = {
        .profile = profile,
        .root_image_identifier = "default-root",
        .terminal_identifier = "default-terminal",
    };
    struct OrlixBootInput input;

    if (OrlixPrepareBootInput(&config, &input) != ORLIX_BOOT_STATUS_OK) {
        return 1;
    }
    if (input.profile != profile) {
        return 2;
    }
    if (expect_string(input.profile_dtb_path, expected_dtb) != 0) {
        return 3;
    }
    if (expect_string(input.console_device, expected_console) != 0) {
        return 4;
    }
    if (expect_string(input.root_device, expected_root) != 0) {
        return 5;
    }
    if (expect_string(input.kernel_cmdline, expected_cmdline) != 0) {
        return 6;
    }
    if (input.root_image_identifier != config.root_image_identifier) {
        return 7;
    }
    if (input.terminal_identifier != config.terminal_identifier) {
        return 8;
    }

    return 0;
}

int main(void)
{
    struct OrlixBootConfig config = {
        .profile = ORLIX_BOOT_PROFILE_APPSTORE,
        .root_image_identifier = "default-root",
        .terminal_identifier = "default-terminal",
    };
    struct OrlixBootInput input;

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

    if (OrlixPrepareBootInput(0, &input) != ORLIX_BOOT_STATUS_INVALID_CONFIG) {
        return 8;
    }

    if (OrlixPrepareBootInput(&config, 0) != ORLIX_BOOT_STATUS_INVALID_CONFIG) {
        return 9;
    }

    if (expect_profile_input(ORLIX_BOOT_PROFILE_APPSTORE,
                             "arch/orlix/boot/dts/appstore.dtb",
                             "ttyS0",
                             "/dev/ram0",
                             "console=ttyS0 root=/dev/ram0 rw orlix.profile=appstore") != 0) {
        return 10;
    }

    if (expect_profile_input(ORLIX_BOOT_PROFILE_DEVELOPMENT,
                             "arch/orlix/boot/dts/development.dtb",
                             "ttyS0",
                             "/dev/ram0",
                             "console=ttyS0 root=/dev/ram0 rw debug ignore_loglevel orlix.profile=development") != 0) {
        return 11;
    }

    if (expect_profile_input(ORLIX_BOOT_PROFILE_ENTERPRISE,
                             "arch/orlix/boot/dts/enterprise.dtb",
                             "ttyS0",
                             "/dev/ram0",
                             "console=ttyS0 root=/dev/ram0 rw orlix.profile=enterprise") != 0) {
        return 12;
    }

    if (OrlixBoot(&config) != ORLIX_BOOT_STATUS_UNAVAILABLE) {
        return 13;
    }

    return 0;
}

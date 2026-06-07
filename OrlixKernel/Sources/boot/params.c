#include "boot/input.h"

struct OrlixProfileBootDefaults {
    enum OrlixBootProfile profile;
    const char *profile_dtb_path;
    const char *kernel_cmdline;
    const char *root_device;
    const char *console_device;
};

static const struct OrlixProfileBootDefaults OrlixProfileBootDefaultsTable[] = {
    {
        .profile = ORLIX_BOOT_PROFILE_RELEASE,
        .profile_dtb_path = "arch/orlix/boot/dts/release.dtb",
        .kernel_cmdline = "console=ttyS0 console=hvc0 root=/dev/vda rootfstype=ext4 ro orlix.profile=release",
        .root_device = "/dev/vda",
        .console_device = "hvc0",
    },
    {
        .profile = ORLIX_BOOT_PROFILE_DEVELOPMENT,
        .profile_dtb_path = "arch/orlix/boot/dts/development.dtb",
        .kernel_cmdline = "console=ttyS0 console=hvc0 rdinit=/init orlix.root=overlay debug ignore_loglevel orlix.profile=development",
        .root_device = "/dev/vda",
        .console_device = "hvc0",
    },
};

static int OrlixBootStringIsPresent(const char *value)
{
    return value && value[0] != '\0';
}

static const struct OrlixProfileBootDefaults *OrlixProfileBootDefaultsFor(
    enum OrlixBootProfile profile)
{
    unsigned long index;

    for (index = 0; index < sizeof(OrlixProfileBootDefaultsTable) / sizeof(OrlixProfileBootDefaultsTable[0]); index++) {
        if (OrlixProfileBootDefaultsTable[index].profile == profile) {
            return &OrlixProfileBootDefaultsTable[index];
        }
    }

    return 0;
}

__attribute__((visibility("hidden"))) int OrlixPrepareBootInput(
    const struct OrlixBootConfig *config,
    struct OrlixBootInput *input)
{
    const struct OrlixProfileBootDefaults *defaults;

    if (!config || !input) {
        return ORLIX_BOOT_STATUS_INVALID_CONFIG;
    }
    if (!OrlixBootStringIsPresent(config->root_image_identifier)) {
        return ORLIX_BOOT_STATUS_INVALID_CONFIG;
    }
    if (!OrlixBootStringIsPresent(config->terminal_identifier)) {
        return ORLIX_BOOT_STATUS_INVALID_CONFIG;
    }

    defaults = OrlixProfileBootDefaultsFor(config->profile);
    if (!defaults) {
        return ORLIX_BOOT_STATUS_INVALID_CONFIG;
    }

    input->profile = config->profile;
    input->profile_dtb_path = defaults->profile_dtb_path;
    input->kernel_cmdline = OrlixBootStringIsPresent(config->kernel_cmdline) ?
        config->kernel_cmdline : defaults->kernel_cmdline;
    input->root_device = defaults->root_device;
    input->console_device = defaults->console_device;
    input->root_image_identifier = config->root_image_identifier;
    input->terminal_identifier = config->terminal_identifier;

    return ORLIX_BOOT_STATUS_OK;
}

__attribute__((visibility("hidden"))) int OrlixPrepareBootConfig(const struct OrlixBootConfig *config)
{
    struct OrlixBootInput input;

    return OrlixPrepareBootInput(config, &input);
}

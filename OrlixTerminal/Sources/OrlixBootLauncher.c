#include "OrlixBootLauncher.h"

#include "OrlixKernel.h"

#include <string.h>

static int OrlixTerminalBootProfile(enum OrlixBootProfile profile)
{
    static const char root_image_identifier[] = "orlix.bundle.rootfs";
    static const char terminal_identifier[] = "orlix.terminal.main";
    struct OrlixBootConfig config = {
        .profile = profile,
        .root_image_identifier = root_image_identifier,
        .terminal_identifier = terminal_identifier,
    };

    return OrlixBoot(&config);
}

int OrlixTerminalBootProfileNamed(const char *profile_name)
{
    if (!profile_name) {
        return ORLIX_BOOT_STATUS_INVALID_CONFIG;
    }
    if (strcmp(profile_name, "appstore") == 0) {
        return OrlixTerminalBootProfile(ORLIX_BOOT_PROFILE_APPSTORE);
    }
    if (strcmp(profile_name, "development") == 0) {
        return OrlixTerminalBootProfile(ORLIX_BOOT_PROFILE_DEVELOPMENT);
    }

    return ORLIX_BOOT_STATUS_INVALID_CONFIG;
}

const char *OrlixTerminalBootStatusMessage(int status)
{
    switch (status) {
    case ORLIX_BOOT_STATUS_OK:
        return "Orlix boot entered the Linux kernel path.";
    case ORLIX_BOOT_STATUS_INVALID_CONFIG:
        return "Orlix bootloader rejected the boot config.";
    case ORLIX_BOOT_STATUS_UNAVAILABLE:
        return "Orlix boot handoff is not wired to iOS-hosted Linux execution yet.";
    default:
        return "Orlix bootloader returned an unknown status.";
    }
}

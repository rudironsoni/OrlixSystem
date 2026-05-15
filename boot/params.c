#include "OrlixKernel.h"

static int OrlixBootProfileIsValid(enum OrlixBootProfile profile) {
    switch (profile) {
    case ORLIX_BOOT_PROFILE_APPSTORE:
    case ORLIX_BOOT_PROFILE_DEVELOPMENT:
    case ORLIX_BOOT_PROFILE_ENTERPRISE:
        return 1;
    }
    return 0;
}

static int OrlixBootStringIsPresent(const char *value) {
    return value && value[0] != '\0';
}

__attribute__((visibility("hidden"))) int OrlixPrepareBootConfig(const struct OrlixBootConfig *config) {
    if (!config) {
        return ORLIX_BOOT_STATUS_INVALID_CONFIG;
    }
    if (!OrlixBootProfileIsValid(config->profile)) {
        return ORLIX_BOOT_STATUS_INVALID_CONFIG;
    }
    if (!OrlixBootStringIsPresent(config->root_image_identifier)) {
        return ORLIX_BOOT_STATUS_INVALID_CONFIG;
    }
    if (!OrlixBootStringIsPresent(config->terminal_identifier)) {
        return ORLIX_BOOT_STATUS_INVALID_CONFIG;
    }
    return ORLIX_BOOT_STATUS_OK;
}

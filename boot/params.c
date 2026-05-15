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
        return -1;
    }
    if (!OrlixBootProfileIsValid(config->profile)) {
        return -1;
    }
    if (!OrlixBootStringIsPresent(config->root_image_identifier)) {
        return -1;
    }
    if (!OrlixBootStringIsPresent(config->terminal_identifier)) {
        return -1;
    }
    return 0;
}

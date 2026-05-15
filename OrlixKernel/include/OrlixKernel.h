#ifndef ORLIX_KERNEL_H
#define ORLIX_KERNEL_H

#ifdef __cplusplus
extern "C" {
#endif

enum OrlixBootProfile {
    ORLIX_BOOT_PROFILE_APPSTORE = 0,
    ORLIX_BOOT_PROFILE_DEVELOPMENT = 1,
    ORLIX_BOOT_PROFILE_ENTERPRISE = 2,
};

struct OrlixBootConfig {
    enum OrlixBootProfile profile;
    const char *root_image_identifier;
    const char *terminal_identifier;
};

__attribute__((visibility("default"))) int OrlixBoot(const struct OrlixBootConfig *config);

#ifdef __cplusplus
}
#endif

#endif

#include "boot/handoff.h"
#include "boot/payload.h"
#include "OrlixHostAdapter/boot/resources.h"

struct OrlixLinuxBootParams {
    const char *cmdline;
    const char *profile_dtb_path;
    unsigned long memory_base;
    unsigned long memory_size;
    const void *initrd_base;
    unsigned long initrd_size;
    const void *dtb_base;
    unsigned long dtb_size;
    const char *root_device;
    const char *console_device;
    unsigned long flags;
};

static int OrlixLinuxBootStringIsPresent(const char *value)
{
    return value && value[0] != '\0';
}

static int OrlixEnterLinux(const struct OrlixLinuxBootParams *params)
{
    if (!params || !OrlixLinuxBootStringIsPresent(params->cmdline) ||
        !OrlixLinuxBootStringIsPresent(params->profile_dtb_path) ||
        !params->dtb_base || params->dtb_size == 0 ||
        !OrlixLinuxBootStringIsPresent(params->root_device) ||
        !OrlixLinuxBootStringIsPresent(params->console_device)) {
        return ORLIX_BOOT_STATUS_INVALID_CONFIG;
    }

    /*
     * OK here means the app-hosted bootloader reached a validated Linux-shaped
     * handoff, not that Linux has reached init yet.
     */
    return ORLIX_BOOT_STATUS_OK;
}

#if defined(ORLIX_BOOT_TESTING)
static struct OrlixBootInput last_handoff;
static int handoff_count;
static int has_last_handoff;
#endif

__attribute__((visibility("hidden"))) int OrlixBootHandoff(
    const struct OrlixBootInput *input)
{
    struct OrlixLinuxBootParams params = { 0 };
    struct OrlixHostResource profile_dtb = { 0 };
    int status;

    if (!input) {
        return ORLIX_BOOT_STATUS_INVALID_CONFIG;
    }

#if defined(ORLIX_BOOT_TESTING)
    last_handoff = *input;
    has_last_handoff = 1;
    handoff_count++;
#endif

    if (!input->profile_dtb_path || input->profile_dtb_path[0] == '\0') {
        return ORLIX_BOOT_STATUS_INVALID_CONFIG;
    }

    if (OrlixSelectRootImage(input->root_image_identifier) != 0) {
        return ORLIX_BOOT_STATUS_INVALID_CONFIG;
    }

    params.cmdline = input->kernel_cmdline;
    params.profile_dtb_path = input->profile_dtb_path;
    if (OrlixHostLoadKernelPayloadResource(params.profile_dtb_path, &profile_dtb) != 0) {
        return ORLIX_BOOT_STATUS_INVALID_CONFIG;
    }
    if (OrlixLoadDeviceTree(profile_dtb.data, profile_dtb.size) != 0) {
        OrlixHostFreeResource(&profile_dtb);
        return ORLIX_BOOT_STATUS_INVALID_CONFIG;
    }
    params.dtb_base = profile_dtb.data;
    params.dtb_size = profile_dtb.size;
    params.root_device = input->root_device;
    params.console_device = input->console_device;

    status = OrlixEnterLinux(&params);
    OrlixHostFreeResource(&profile_dtb);
    return status;
}

#if defined(ORLIX_BOOT_TESTING)
void OrlixBootResetHandoff(void)
{
    has_last_handoff = 0;
    handoff_count = 0;
}

int OrlixBootHandoffCount(void)
{
    return handoff_count;
}

const struct OrlixBootInput *OrlixBootLastHandoff(void)
{
    return has_last_handoff ? &last_handoff : 0;
}
#endif

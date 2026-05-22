#include "boot/handoff.h"
#include "boot/payload.h"
#include "OrlixHostAdapter/boot/resources.h"
#include <asm/boot.h>

static int OrlixLinuxBootStringIsPresent(const char *value)
{
    return value && value[0] != '\0';
}

static int OrlixEnterLinux(const struct boot_params *params)
{
    int status;

    if (!params || !OrlixLinuxBootStringIsPresent(params->cmdline) ||
        !params->dtb_base || params->dtb_size == 0 ||
        !OrlixLinuxBootStringIsPresent(params->root_device) ||
        !OrlixLinuxBootStringIsPresent(params->console_device)) {
        return ORLIX_BOOT_STATUS_INVALID_CONFIG;
    }

    status = arch_boot_entry(params);
    if (status == ORLIX_ARCH_BOOT_OK) {
        return ORLIX_BOOT_STATUS_OK;
    }
    if (status == ORLIX_ARCH_BOOT_UNAVAILABLE) {
        return ORLIX_BOOT_STATUS_UNAVAILABLE;
    }

    return ORLIX_BOOT_STATUS_INVALID_CONFIG;
}

#if defined(ORLIX_BOOT_TESTING)
static struct OrlixBootInput last_handoff;
static int handoff_count;
static int has_last_handoff;
#endif

__attribute__((visibility("hidden"))) int OrlixBootHandoff(
    const struct OrlixBootInput *input)
{
    struct boot_params params = { 0 };
    struct OrlixHostResource profile_dtb = { 0 };
    struct OrlixHostResource root_image = { 0 };
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
    if (OrlixHostSelectBootBlockImages(input->root_image_identifier) != 0) {
        return ORLIX_BOOT_STATUS_INVALID_CONFIG;
    }

    params.cmdline = input->kernel_cmdline;
    if (OrlixHostLoadKernelPayloadResource(input->profile_dtb_path, &profile_dtb) != 0) {
        return ORLIX_BOOT_STATUS_INVALID_CONFIG;
    }
    if (OrlixHostLoadRootImageResource(input->root_image_identifier, &root_image) != 0) {
        OrlixHostFreeResource(&profile_dtb);
        return ORLIX_BOOT_STATUS_INVALID_CONFIG;
    }
    if (OrlixLoadDeviceTree(profile_dtb.data, profile_dtb.size) != 0) {
        OrlixHostFreeResource(&root_image);
        OrlixHostFreeResource(&profile_dtb);
        return ORLIX_BOOT_STATUS_INVALID_CONFIG;
    }
    if (OrlixLoadInitrd(root_image.data, root_image.size) != 0) {
        OrlixHostFreeResource(&root_image);
        OrlixHostFreeResource(&profile_dtb);
        return ORLIX_BOOT_STATUS_INVALID_CONFIG;
    }
    params.dtb_base = profile_dtb.data;
    params.dtb_size = profile_dtb.size;
    params.initrd_base = root_image.data;
    params.initrd_size = root_image.size;
    params.root_device = input->root_device;
    params.console_device = input->console_device;

    status = OrlixEnterLinux(&params);
    OrlixHostFreeResource(&root_image);
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

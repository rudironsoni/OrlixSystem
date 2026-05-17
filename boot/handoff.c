#include "boot/handoff.h"
#include "boot/payload.h"
#include "OrlixHostAdapter/boot/resources.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

struct OrlixLinuxBootParams {
    const char *cmdline;
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

static const char *OrlixPathBasename(const char *path)
{
    const char *basename;

    if (!path) {
        return 0;
    }

    basename = strrchr(path, '/');
    return basename ? basename + 1 : path;
}

static int OrlixEnterLinux(const struct OrlixHostResource *kernel_image,
                           const struct OrlixLinuxBootParams *params)
{
    (void)kernel_image;
    (void)params;
    return ORLIX_BOOT_STATUS_UNAVAILABLE;
}

#if defined(ORLIX_BOOT_TESTING)
static struct OrlixBootInput last_handoff;
static int handoff_count;
static int has_last_handoff;
#endif

__attribute__((visibility("hidden"))) int OrlixBootHandoff(
    const struct OrlixBootInput *input)
{
    struct OrlixHostResource kernel_image = { 0 };
    struct OrlixHostResource dtb = { 0 };
    struct OrlixLinuxBootParams params = { 0 };
    char dtb_resource[PATH_MAX];
    const char *dtb_name;
    int dtb_written;
    int status = ORLIX_BOOT_STATUS_UNAVAILABLE;

    if (!input) {
        return ORLIX_BOOT_STATUS_INVALID_CONFIG;
    }

#if defined(ORLIX_BOOT_TESTING)
    last_handoff = *input;
    has_last_handoff = 1;
    handoff_count++;
#endif

    dtb_name = OrlixPathBasename(input->profile_dtb_path);
    if (!dtb_name || dtb_name[0] == '\0') {
        return ORLIX_BOOT_STATUS_INVALID_CONFIG;
    }
    dtb_written = snprintf(dtb_resource, sizeof(dtb_resource), "dtbs/%s", dtb_name);
    if (dtb_written < 0 || (size_t)dtb_written >= sizeof(dtb_resource)) {
        return ORLIX_BOOT_STATUS_INVALID_CONFIG;
    }

    if (OrlixSelectRootImage(input->root_image_identifier) != 0) {
        return ORLIX_BOOT_STATUS_INVALID_CONFIG;
    }
    if (OrlixHostLoadKernelPayloadResource("vmlinux", &kernel_image) != 0) {
        goto out;
    }
    if (OrlixHostLoadKernelPayloadResource(dtb_resource, &dtb) != 0) {
        goto out;
    }
    if (OrlixLoadKernelImage(kernel_image.data, kernel_image.size) != 0) {
        goto out;
    }
    if (OrlixLoadDeviceTree(dtb.data, dtb.size) != 0) {
        goto out;
    }

    params.cmdline = input->kernel_cmdline;
    params.dtb_base = dtb.data;
    params.dtb_size = dtb.size;
    params.root_device = input->root_device;
    params.console_device = input->console_device;

    status = OrlixEnterLinux(&kernel_image, &params);

out:
    OrlixHostFreeResource(&dtb);
    OrlixHostFreeResource(&kernel_image);
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

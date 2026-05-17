#include "boot/handoff.h"
#include "boot/payload.h"

#include <CoreFoundation/CoreFoundation.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct OrlixLoadedResource {
    void *data;
    unsigned long size;
};

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

static int OrlixCopyPayloadRootPath(char *path, size_t path_size)
{
    CFBundleRef kernel_bundle;
    CFURLRef payload_url;
    Boolean ok;

    if (!path || path_size == 0) {
        return -1;
    }

    kernel_bundle = CFBundleGetBundleWithIdentifier(CFSTR("org.orlix.OrlixKernel"));
    if (!kernel_bundle) {
        return -1;
    }

    payload_url = CFBundleCopyResourceURL(
        kernel_bundle,
        CFSTR("OrlixKernelPayload"),
        CFSTR("bundle"),
        0);
    if (!payload_url) {
        return -1;
    }

    ok = CFURLGetFileSystemRepresentation(payload_url, true, (UInt8 *)path, path_size);
    CFRelease(payload_url);

    return ok ? 0 : -1;
}

static int OrlixCopyPayloadResourcePath(const char *resource,
                                        char *path,
                                        size_t path_size)
{
    char payload_root[PATH_MAX];
    int written;

    if (!resource || resource[0] == '\0' || !path || path_size == 0) {
        return -1;
    }
    if (OrlixCopyPayloadRootPath(payload_root, sizeof(payload_root)) != 0) {
        return -1;
    }

    written = snprintf(path, path_size, "%s/%s", payload_root, resource);
    if (written < 0 || (size_t)written >= path_size) {
        return -1;
    }

    return 0;
}

static int OrlixReadResourceFile(const char *path, struct OrlixLoadedResource *resource)
{
    FILE *file;
    long length;
    void *data;
    size_t read_count;

    if (!path || !resource) {
        return -1;
    }

    file = fopen(path, "rb");
    if (!file) {
        return -1;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return -1;
    }
    length = ftell(file);
    if (length <= 0) {
        fclose(file);
        return -1;
    }
    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return -1;
    }

    data = malloc((size_t)length);
    if (!data) {
        fclose(file);
        return -1;
    }

    read_count = fread(data, 1, (size_t)length, file);
    fclose(file);
    if (read_count != (size_t)length) {
        free(data);
        return -1;
    }

    resource->data = data;
    resource->size = (unsigned long)length;
    return 0;
}

static int OrlixLoadPayloadResource(const char *resource,
                                    struct OrlixLoadedResource *loaded)
{
    char path[PATH_MAX];

    if (!loaded) {
        return -1;
    }

    loaded->data = 0;
    loaded->size = 0;
    if (OrlixCopyPayloadResourcePath(resource, path, sizeof(path)) != 0) {
        return -1;
    }

    return OrlixReadResourceFile(path, loaded);
}

static void OrlixFreeLoadedResource(struct OrlixLoadedResource *resource)
{
    if (!resource) {
        return;
    }

    free(resource->data);
    resource->data = 0;
    resource->size = 0;
}

static int OrlixEnterLinux(const struct OrlixLoadedResource *kernel_image,
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
    struct OrlixLoadedResource kernel_image = { 0 };
    struct OrlixLoadedResource dtb = { 0 };
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
    if (OrlixLoadPayloadResource("vmlinux", &kernel_image) != 0) {
        goto out;
    }
    if (OrlixLoadPayloadResource(dtb_resource, &dtb) != 0) {
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
    OrlixFreeLoadedResource(&dtb);
    OrlixFreeLoadedResource(&kernel_image);
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

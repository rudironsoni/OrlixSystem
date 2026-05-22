#include "OrlixHostAdapter/boot/resources.h"

#include <CoreFoundation/CoreFoundation.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *OrlixHostRootImageResourceForIdentifier(const char *identifier)
{
    if (!identifier) {
        return 0;
    }
    if (strcmp(identifier, "orlix.bundle.rootfs") == 0) {
        return "rootfs/initramfs.cpio.gz";
    }
    return 0;
}

static int OrlixHostCopyPayloadRootPath(char *path, size_t path_size)
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

static int OrlixHostCopyPayloadResourcePath(const char *resource,
                                            char *path,
                                            size_t path_size)
{
    char payload_root[PATH_MAX];
    int written;

    if (!resource || resource[0] == '\0' || !path || path_size == 0) {
        return -1;
    }
    if (OrlixHostCopyPayloadRootPath(payload_root, sizeof(payload_root)) != 0) {
        return -1;
    }

    written = snprintf(path, path_size, "%s/%s", payload_root, resource);
    if (written < 0 || (size_t)written >= path_size) {
        return -1;
    }

    return 0;
}

static int OrlixHostReadResourceFile(const char *path,
                                     struct OrlixHostResource *resource)
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

__attribute__((visibility("hidden"))) int OrlixHostLoadKernelPayloadResource(
    const char *resource,
    struct OrlixHostResource *loaded)
{
    char path[PATH_MAX];

    if (!loaded) {
        return -1;
    }

    loaded->data = 0;
    loaded->size = 0;
    if (OrlixHostCopyPayloadResourcePath(resource, path, sizeof(path)) != 0) {
        return -1;
    }

    return OrlixHostReadResourceFile(path, loaded);
}

__attribute__((visibility("hidden"))) int OrlixHostLoadRootImageResource(
    const char *identifier,
    struct OrlixHostResource *loaded)
{
    const char *resource = OrlixHostRootImageResourceForIdentifier(identifier);

    if (!resource) {
        return -1;
    }

    return OrlixHostLoadKernelPayloadResource(resource, loaded);
}

__attribute__((visibility("hidden"))) void OrlixHostFreeResource(
    struct OrlixHostResource *resource)
{
    if (!resource) {
        return;
    }

    free(resource->data);
    resource->data = 0;
    resource->size = 0;
}

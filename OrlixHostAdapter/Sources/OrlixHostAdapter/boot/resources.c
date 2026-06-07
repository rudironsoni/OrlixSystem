#include "OrlixHostAdapter/boot/resources.h"
#include "OrlixHostAdapter/runtime/host_tls.h"

#include <CoreFoundation/CoreFoundation.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define ORLIX_HOST_BLOCK_SECTOR_SIZE 512ULL
#define ORLIX_HOST_BLOCK_DEVICE_COUNT 2
#define ORLIX_HOST_BASE_BLOCK_DEVICE 0
#define ORLIX_HOST_STATE_BLOCK_DEVICE 1
#define ORLIX_HOST_STATE_BLOCK_BYTES (16ULL * 1024ULL * 1024ULL)

static char OrlixHostSelectedBlockPaths[ORLIX_HOST_BLOCK_DEVICE_COUNT][PATH_MAX];
static unsigned long long OrlixHostSelectedBlockBytes[ORLIX_HOST_BLOCK_DEVICE_COUNT];
static int OrlixHostSelectedBlockWritable[ORLIX_HOST_BLOCK_DEVICE_COUNT];

static const char *OrlixHostInitrdResourceForIdentifier(const char *identifier)
{
    if (!identifier) {
        return 0;
    }
    if (strcmp(identifier, "orlix.bundle.rootfs") == 0) {
        return "rootfs/initramfs.cpio.gz";
    }
    if (strcmp(identifier, "orlix.test.kselftest.rootfs") == 0 ||
        strcmp(identifier, "orlix.test.mlibc.rootfs") == 0 ||
        strcmp(identifier, "orlix.test.coreutils.rootfs") == 0) {
        return "rootfs/initramfs.cpio.gz";
    }
    return 0;
}

static const char *OrlixHostTestBundleForIdentifier(const char *identifier)
{
    if (!identifier) {
        return 0;
    }
    if (strcmp(identifier, "orlix.test.kselftest.rootfs") == 0) {
        return "OrlixTestInitramfs";
    }
    if (strcmp(identifier, "orlix.test.mlibc.rootfs") == 0) {
        return "OrlixMLibCTestInitramfs";
    }
    if (strcmp(identifier, "orlix.test.coreutils.rootfs") == 0) {
        return "CoreutilsTestInitramfs";
    }
    return 0;
}

static int OrlixHostIdentifierUsesBootBlocks(const char *identifier)
{
    return identifier && strcmp(identifier, "orlix.bundle.rootfs") == 0;
}

static const char *OrlixHostBaseBlockResourceForIdentifier(const char *identifier)
{
    if (!identifier) {
        return 0;
    }
    if (strcmp(identifier, "orlix.bundle.rootfs") == 0) {
        return "rootfs/base.ext4";
    }
    return 0;
}

static const char *OrlixHostStateBlockResourceForIdentifier(const char *identifier)
{
    if (!identifier) {
        return 0;
    }
    if (strcmp(identifier, "orlix.bundle.rootfs") == 0) {
        return "rootfs/state.ext4";
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

static int OrlixHostCopyMainBundleResourceRootPath(const char *name,
                                                   const char *extension,
                                                   char *path,
                                                   size_t path_size)
{
    CFBundleRef main_bundle;
    CFStringRef resource_name;
    CFStringRef resource_extension;
    CFURLRef resource_url;
    Boolean ok;

    if (!name || !extension || !path || path_size == 0) {
        return -1;
    }

    main_bundle = CFBundleGetMainBundle();
    if (!main_bundle) {
        return -1;
    }

    resource_name = CFStringCreateWithCString(
        kCFAllocatorDefault,
        name,
        kCFStringEncodingUTF8);
    resource_extension = CFStringCreateWithCString(
        kCFAllocatorDefault,
        extension,
        kCFStringEncodingUTF8);
    if (!resource_name || !resource_extension) {
        if (resource_name) {
            CFRelease(resource_name);
        }
        if (resource_extension) {
            CFRelease(resource_extension);
        }
        return -1;
    }

    resource_url = CFBundleCopyResourceURL(
        main_bundle,
        resource_name,
        resource_extension,
        0);
    CFRelease(resource_name);
    CFRelease(resource_extension);
    if (!resource_url) {
        return -1;
    }

    ok = CFURLGetFileSystemRepresentation(
        resource_url,
        true,
        (UInt8 *)path,
        path_size);
    CFRelease(resource_url);
    return ok ? 0 : -1;
}

static int OrlixHostCopyTestBundleResourcePath(const char *bundle_name,
                                               const char *resource,
                                               char *path,
                                               size_t path_size)
{
    char bundle_root[PATH_MAX];
    int written;

    if (!bundle_name || !resource || !path || path_size == 0) {
        return -1;
    }
    if (OrlixHostCopyMainBundleResourceRootPath(bundle_name,
                                               "bundle",
                                               bundle_root,
                                               sizeof(bundle_root)) != 0) {
        return -1;
    }

    written = snprintf(path, path_size, "%s/%s", bundle_root, resource);
    if (written < 0 || (size_t)written >= path_size) {
        return -1;
    }

    return 0;
}

static int OrlixHostResourceFileSize(const char *path,
                                     unsigned long long *size)
{
    FILE *file;
    long length;

    if (!path || !size) {
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
    fclose(file);
    if (length <= 0) {
        return -1;
    }

    *size = (unsigned long long)length;
    return 0;
}

static void OrlixHostClearSelectedBlockImages(void)
{
    memset(OrlixHostSelectedBlockPaths, 0, sizeof(OrlixHostSelectedBlockPaths));
    memset(OrlixHostSelectedBlockBytes, 0, sizeof(OrlixHostSelectedBlockBytes));
    memset(OrlixHostSelectedBlockWritable, 0, sizeof(OrlixHostSelectedBlockWritable));
}

static int OrlixHostCopySelectedBlockPath(unsigned int device, const char *path)
{
    size_t length;

    if (device >= ORLIX_HOST_BLOCK_DEVICE_COUNT || !path) {
        return -1;
    }

    length = strlen(path);
    if (length >= PATH_MAX) {
        return -1;
    }

    memcpy(OrlixHostSelectedBlockPaths[device], path, length + 1);
    return 0;
}

static int OrlixHostBlockDeviceIsSelected(unsigned int device)
{
    return device < ORLIX_HOST_BLOCK_DEVICE_COUNT &&
           OrlixHostSelectedBlockPaths[device][0] != '\0' &&
           OrlixHostSelectedBlockBytes[device] != 0;
}

static int OrlixHostEnsureDirectory(const char *path)
{
    struct stat state;

    if (!path || path[0] == '\0') {
        return -1;
    }

    if (mkdir(path, 0700) == 0) {
        return 0;
    }
    if (errno != EEXIST) {
        return -1;
    }

    return stat(path, &state) == 0 && S_ISDIR(state.st_mode) ? 0 : -1;
}

static int OrlixHostCopyStateBlockPath(char *path, size_t path_size)
{
    const char *home = getenv("HOME");
    char library[PATH_MAX];
    char application_support[PATH_MAX];
    char orlix[PATH_MAX];
    int written;

    if (!home || home[0] == '\0' || !path || path_size == 0) {
        return -1;
    }

    written = snprintf(library, sizeof(library), "%s/Library", home);
    if (written < 0 || (size_t)written >= sizeof(library)) {
        return -1;
    }
    if (OrlixHostEnsureDirectory(library) != 0) {
        return -1;
    }

    written = snprintf(application_support,
                       sizeof(application_support),
                       "%s/Application Support",
                       library);
    if (written < 0 || (size_t)written >= sizeof(application_support)) {
        return -1;
    }
    if (OrlixHostEnsureDirectory(application_support) != 0) {
        return -1;
    }

    written = snprintf(orlix, sizeof(orlix), "%s/Orlix", application_support);
    if (written < 0 || (size_t)written >= sizeof(orlix)) {
        return -1;
    }
    if (OrlixHostEnsureDirectory(orlix) != 0) {
        return -1;
    }

    written = snprintf(path, path_size, "%s/root-state.img", orlix);
    return written >= 0 && (size_t)written < path_size ? 0 : -1;
}

static int OrlixHostCopyFile(const char *source, const char *target)
{
    unsigned char buffer[16384];
    FILE *input;
    FILE *output;
    size_t count;
    int result = -1;

    if (!source || !target) {
        return -1;
    }

    input = fopen(source, "rb");
    if (!input) {
        return -1;
    }
    output = fopen(target, "wb");
    if (!output) {
        fclose(input);
        return -1;
    }

    while ((count = fread(buffer, 1, sizeof(buffer), input)) > 0) {
        if (fwrite(buffer, 1, count, output) != count) {
            goto out;
        }
    }
    if (ferror(input) || fflush(output) != 0) {
        goto out;
    }

    result = 0;

out:
    if (fclose(output) != 0) {
        result = -1;
    }
    fclose(input);
    return result;
}

static int OrlixHostStateBlockHasExt4Magic(const char *path)
{
    unsigned char magic[2];
    FILE *file;
    size_t count;

    if (!path) {
        return 0;
    }

    file = fopen(path, "rb");
    if (!file) {
        return 0;
    }
    if (fseeko(file, 1080, SEEK_SET) != 0) {
        fclose(file);
        return 0;
    }

    count = fread(magic, 1, sizeof(magic), file);
    fclose(file);

    return count == sizeof(magic) && magic[0] == 0x53 && magic[1] == 0xef;
}

static int OrlixHostEnsureStateBlockFile(const char *path,
                                         const char *template_path,
                                         unsigned long long *size)
{
    unsigned long long target_size;
    struct stat state;
    int fd;

    if (!path || !template_path || !size) {
        return -1;
    }

    if (!OrlixHostStateBlockHasExt4Magic(path) &&
        OrlixHostCopyFile(template_path, path) != 0) {
        return -1;
    }

    fd = open(path, O_RDWR | O_CREAT, 0600);
    if (fd < 0) {
        return -1;
    }
    if (fstat(fd, &state) != 0 ||
        !S_ISREG(state.st_mode) ||
        state.st_size < 0) {
        close(fd);
        return -1;
    }

    target_size = (unsigned long long)state.st_size;
    if (target_size < ORLIX_HOST_STATE_BLOCK_BYTES) {
        target_size = ORLIX_HOST_STATE_BLOCK_BYTES;
    }
    if (target_size % ORLIX_HOST_BLOCK_SECTOR_SIZE) {
        target_size = ((target_size + ORLIX_HOST_BLOCK_SECTOR_SIZE - 1) /
                       ORLIX_HOST_BLOCK_SECTOR_SIZE) *
                      ORLIX_HOST_BLOCK_SECTOR_SIZE;
    }
    if (target_size > (unsigned long long)LLONG_MAX ||
        (unsigned long long)state.st_size != target_size) {
        if (target_size > (unsigned long long)LLONG_MAX ||
            ftruncate(fd, (off_t)target_size) != 0) {
            close(fd);
            return -1;
        }
    }

    close(fd);
    *size = target_size;
    return target_size ? 0 : -1;
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

__attribute__((visibility("hidden"))) int OrlixHostLoadInitrdResource(
    const char *identifier,
    struct OrlixHostResource *loaded)
{
    const char *resource = OrlixHostInitrdResourceForIdentifier(identifier);
    const char *test_bundle = OrlixHostTestBundleForIdentifier(identifier);
    char path[PATH_MAX];

    if (!resource) {
        return -1;
    }
    if (test_bundle) {
        if (!loaded) {
            return -1;
        }
        loaded->data = 0;
        loaded->size = 0;
        if (OrlixHostCopyTestBundleResourcePath(test_bundle,
                                                resource,
                                                path,
                                                sizeof(path)) != 0) {
            return -1;
        }
        return OrlixHostReadResourceFile(path, loaded);
    }

    return OrlixHostLoadKernelPayloadResource(resource, loaded);
}

__attribute__((visibility("hidden"))) int OrlixHostSelectBootBlockImages(
    const char *identifier)
{
    const char *resource = OrlixHostBaseBlockResourceForIdentifier(identifier);
    const char *state_resource = OrlixHostStateBlockResourceForIdentifier(identifier);
    unsigned long long base_size = 0;
    unsigned long long state_size = 0;
    char base_path[PATH_MAX];
    char state_template_path[PATH_MAX];
    char state_path[PATH_MAX];

    OrlixHostClearSelectedBlockImages();

    if (!OrlixHostIdentifierUsesBootBlocks(identifier)) {
        return OrlixHostInitrdResourceForIdentifier(identifier) ? 0 : -1;
    }
    if (!resource || !state_resource) {
        return -1;
    }
    if (OrlixHostCopyPayloadResourcePath(resource, base_path, sizeof(base_path)) != 0) {
        return -1;
    }
    if (OrlixHostCopyPayloadResourcePath(state_resource,
                                         state_template_path,
                                         sizeof(state_template_path)) != 0) {
        return -1;
    }
    if (OrlixHostResourceFileSize(base_path, &base_size) != 0) {
        return -1;
    }
    if (OrlixHostCopyStateBlockPath(state_path, sizeof(state_path)) != 0) {
        return -1;
    }
    if (OrlixHostEnsureStateBlockFile(state_path,
                                      state_template_path,
                                      &state_size) != 0) {
        return -1;
    }
    if (OrlixHostCopySelectedBlockPath(ORLIX_HOST_BASE_BLOCK_DEVICE, base_path) != 0 ||
        OrlixHostCopySelectedBlockPath(ORLIX_HOST_STATE_BLOCK_DEVICE, state_path) != 0) {
        OrlixHostClearSelectedBlockImages();
        return -1;
    }

    OrlixHostSelectedBlockBytes[ORLIX_HOST_BASE_BLOCK_DEVICE] = base_size;
    OrlixHostSelectedBlockBytes[ORLIX_HOST_STATE_BLOCK_DEVICE] = state_size;
    OrlixHostSelectedBlockWritable[ORLIX_HOST_STATE_BLOCK_DEVICE] = 1;
    return 0;
}

__attribute__((visibility("hidden"))) int orlix_host_block_capacity(
    unsigned int device,
    unsigned long long *sectors)
{
    if (!sectors || !OrlixHostBlockDeviceIsSelected(device)) {
        return -1;
    }

    *sectors = (OrlixHostSelectedBlockBytes[device] +
                ORLIX_HOST_BLOCK_SECTOR_SIZE - 1) /
               ORLIX_HOST_BLOCK_SECTOR_SIZE;
    return *sectors ? 0 : -1;
}

__attribute__((visibility("hidden"))) int orlix_host_block_read(
    unsigned int device,
    unsigned long long sector,
    void *buffer,
    unsigned int length)
{
    FILE *file;
    unsigned long long offset;
    unsigned long long capacity_bytes;
    unsigned long long available;
    unsigned int file_read_length;
    size_t read_count;
    unsigned long active_tls;
    int result = -1;

    if (!OrlixHostBlockDeviceIsSelected(device) || !buffer || !length ||
        sector > ULLONG_MAX / ORLIX_HOST_BLOCK_SECTOR_SIZE) {
        return -1;
    }

    offset = sector * ORLIX_HOST_BLOCK_SECTOR_SIZE;
    capacity_bytes = ((OrlixHostSelectedBlockBytes[device] +
                       ORLIX_HOST_BLOCK_SECTOR_SIZE - 1) /
                      ORLIX_HOST_BLOCK_SECTOR_SIZE) *
                     ORLIX_HOST_BLOCK_SECTOR_SIZE;
    if (offset > capacity_bytes ||
        length > capacity_bytes - offset ||
        offset > (unsigned long long)LLONG_MAX) {
        return -1;
    }

    active_tls = OrlixHostEnterHostTls();
    memset(buffer, 0, length);
    if (offset >= OrlixHostSelectedBlockBytes[device]) {
        result = 0;
        goto out;
    }

    available = OrlixHostSelectedBlockBytes[device] - offset;
    file_read_length = length;
    if (available < file_read_length) {
        file_read_length = (unsigned int)available;
    }
    file = fopen(OrlixHostSelectedBlockPaths[device], "rb");
    if (!file) {
        goto out;
    }
    if (fseeko(file, (off_t)offset, SEEK_SET) != 0) {
        fclose(file);
        goto out;
    }

    read_count = fread(buffer, 1, file_read_length, file);
    fclose(file);
    result = read_count == file_read_length ? 0 : -1;

out:
    OrlixHostLeaveHostTls(active_tls);
    return result;
}

__attribute__((visibility("hidden"))) int orlix_host_block_write(
    unsigned int device,
    unsigned long long sector,
    const void *buffer,
    unsigned int length)
{
    FILE *file;
    unsigned long long offset;
    unsigned long long capacity_bytes;
    size_t write_count;
    unsigned long active_tls;
    int result = -1;

    if (!OrlixHostBlockDeviceIsSelected(device) ||
        !OrlixHostSelectedBlockWritable[device] ||
        !buffer || !length ||
        sector > ULLONG_MAX / ORLIX_HOST_BLOCK_SECTOR_SIZE) {
        return -1;
    }

    offset = sector * ORLIX_HOST_BLOCK_SECTOR_SIZE;
    capacity_bytes = ((OrlixHostSelectedBlockBytes[device] +
                       ORLIX_HOST_BLOCK_SECTOR_SIZE - 1) /
                      ORLIX_HOST_BLOCK_SECTOR_SIZE) *
                     ORLIX_HOST_BLOCK_SECTOR_SIZE;
    if (offset > capacity_bytes ||
        length > capacity_bytes - offset ||
        offset > (unsigned long long)LLONG_MAX) {
        return -1;
    }

    active_tls = OrlixHostEnterHostTls();
    file = fopen(OrlixHostSelectedBlockPaths[device], "r+b");
    if (!file) {
        goto out;
    }
    if (fseeko(file, (off_t)offset, SEEK_SET) != 0) {
        fclose(file);
        goto out;
    }

    write_count = fwrite(buffer, 1, length, file);
    if (write_count != length) {
        fclose(file);
        goto out;
    }

    result = fclose(file) == 0 ? 0 : -1;

out:
    OrlixHostLeaveHostTls(active_tls);
    return result;
}

__attribute__((visibility("hidden"))) void OrlixHostFreeResource(
    struct OrlixHostResource *resource)
{
    unsigned long active_tls;

    if (!resource) {
        return;
    }

    active_tls = OrlixHostEnterHostTls();
    free(resource->data);
    OrlixHostLeaveHostTls(active_tls);
    resource->data = 0;
    resource->size = 0;
}

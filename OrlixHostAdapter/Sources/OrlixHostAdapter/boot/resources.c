#include "OrlixHostAdapter/boot/resources.h"
#include "OrlixHostAdapter/runtime/host_tls.h"

#include <CoreFoundation/CoreFoundation.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <os/lock.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define ORLIX_HOST_BLOCK_SECTOR_SIZE 512ULL
#define ORLIX_HOST_MAX_BLOCK_DEVICES 8
#define ORLIX_HOST_MAX_ROOT_IMAGES 8

static char OrlixHostSelectedBlockPaths[ORLIX_HOST_MAX_BLOCK_DEVICES][PATH_MAX];
static unsigned long long OrlixHostSelectedBlockBytes[ORLIX_HOST_MAX_BLOCK_DEVICES];
static int OrlixHostSelectedBlockWritable[ORLIX_HOST_MAX_BLOCK_DEVICES];
static os_unfair_lock OrlixHostPayloadRootLock = OS_UNFAIR_LOCK_INIT;
static char OrlixHostPayloadRootPath[PATH_MAX];
static os_unfair_lock OrlixHostRootImagesLock = OS_UNFAIR_LOCK_INIT;

struct OrlixHostRootImage {
    char identifier[PATH_MAX];
    char initrd_bundle_name[PATH_MAX];
    char initrd_bundle_extension[PATH_MAX];
    char initrd_resource[PATH_MAX];
    char base_block_resource[PATH_MAX];
    char state_block_resource[PATH_MAX];
    int block_images_are_files;
    unsigned int base_block_device;
    unsigned int state_block_device;
    unsigned long long state_block_minimum_bytes;
};

static struct OrlixHostRootImage
    OrlixHostRootImages[ORLIX_HOST_MAX_ROOT_IMAGES];
static unsigned int OrlixHostRootImageCount;

static int OrlixHostPathContainsParentReference(const char *path)
{
    const char *cursor;

    if (!path) {
        return 1;
    }
    if (path[0] == '/' || strcmp(path, "..") == 0 ||
        strncmp(path, "../", 3) == 0) {
        return 1;
    }
    cursor = path;
    while ((cursor = strstr(cursor, "/..")) != 0) {
        if (cursor[3] == '\0' || cursor[3] == '/') {
            return 1;
        }
        cursor += 3;
    }

    return 0;
}

static int OrlixHostCopyRequiredString(char *target,
                                       size_t target_size,
                                       const char *source)
{
    size_t length;

    if (!target || target_size == 0 || !source || source[0] == '\0') {
        return -1;
    }
    length = strlen(source);
    if (length >= target_size) {
        return -1;
    }
    memcpy(target, source, length + 1);
    return 0;
}

static int OrlixHostCopyOptionalString(char *target,
                                       size_t target_size,
                                       const char *source)
{
    size_t length;

    if (!target || target_size == 0) {
        return -1;
    }
    if (!source || source[0] == '\0') {
        target[0] = '\0';
        return 0;
    }
    length = strlen(source);
    if (length >= target_size) {
        return -1;
    }
    memcpy(target, source, length + 1);
    return 0;
}

static int OrlixHostCopyRequiredResource(char *target,
                                         size_t target_size,
                                         const char *source)
{
    if (OrlixHostPathContainsParentReference(source)) {
        return -1;
    }
    return OrlixHostCopyRequiredString(target, target_size, source);
}

static int OrlixHostAbsolutePathContainsParentReference(const char *path)
{
    const char *cursor;

    if (!path || path[0] != '/') {
        return 1;
    }
    cursor = path;
    while ((cursor = strstr(cursor, "/..")) != 0) {
        if (cursor[3] == '\0' || cursor[3] == '/') {
            return 1;
        }
        cursor += 3;
    }
    return 0;
}

static int OrlixHostCopyRequiredBlockFilePath(char *target,
                                              size_t target_size,
                                              const char *source)
{
    struct stat state;

    if (OrlixHostAbsolutePathContainsParentReference(source) ||
        OrlixHostCopyRequiredString(target, target_size, source) != 0) {
        return -1;
    }
    if (stat(target, &state) != 0 || !S_ISREG(state.st_mode)) {
        return -1;
    }
    return 0;
}

static int OrlixHostCopyRootImageForIdentifier(
    const char *identifier,
    struct OrlixHostRootImage *root_image)
{
    unsigned int index;

    if (!identifier || !root_image) {
        return -1;
    }

    os_unfair_lock_lock(&OrlixHostRootImagesLock);
    for (index = 0; index < OrlixHostRootImageCount; index++) {
        if (strcmp(OrlixHostRootImages[index].identifier, identifier) == 0) {
            *root_image = OrlixHostRootImages[index];
            os_unfair_lock_unlock(&OrlixHostRootImagesLock);
            return 0;
        }
    }
    os_unfair_lock_unlock(&OrlixHostRootImagesLock);
    return -1;
}

__attribute__((visibility("default"))) int orlix_host_resources_set_payload_root_path(
    const char *path)
{
    struct stat state;
    size_t length;

    if (!path || path[0] == '\0') {
        return -1;
    }
    length = strlen(path);
    if (length >= sizeof(OrlixHostPayloadRootPath)) {
        return -1;
    }
    if (stat(path, &state) != 0 || !S_ISDIR(state.st_mode)) {
        return -1;
    }

    os_unfair_lock_lock(&OrlixHostPayloadRootLock);
    memcpy(OrlixHostPayloadRootPath, path, length + 1);
    os_unfair_lock_unlock(&OrlixHostPayloadRootLock);
    return 0;
}

__attribute__((visibility("default"))) int orlix_host_resources_clear_root_images(void)
{
    os_unfair_lock_lock(&OrlixHostRootImagesLock);
    memset(OrlixHostRootImages, 0, sizeof(OrlixHostRootImages));
    OrlixHostRootImageCount = 0;
    os_unfair_lock_unlock(&OrlixHostRootImagesLock);
    return 0;
}

static int OrlixHostRegisterRootImage(struct OrlixHostRootImage *root_image)
{
    unsigned int index;
    unsigned int target_index;

    if (!root_image ||
        root_image->base_block_device >= ORLIX_HOST_MAX_BLOCK_DEVICES ||
        root_image->state_block_device >= ORLIX_HOST_MAX_BLOCK_DEVICES ||
        root_image->base_block_device == root_image->state_block_device ||
        root_image->state_block_minimum_bytes == 0) {
        return -1;
    }
    if ((root_image->initrd_bundle_name[0] == '\0') !=
        (root_image->initrd_bundle_extension[0] == '\0')) {
        return -1;
    }
    if (strchr(root_image->initrd_bundle_name, '/') ||
        OrlixHostPathContainsParentReference(root_image->initrd_bundle_name) ||
        strchr(root_image->initrd_bundle_extension, '/') ||
        OrlixHostPathContainsParentReference(
            root_image->initrd_bundle_extension)) {
        return -1;
    }

    os_unfair_lock_lock(&OrlixHostRootImagesLock);
    target_index = OrlixHostRootImageCount;
    for (index = 0; index < OrlixHostRootImageCount; index++) {
        if (strcmp(OrlixHostRootImages[index].identifier,
                   root_image->identifier) == 0) {
            target_index = index;
            break;
        }
    }
    if (target_index == OrlixHostRootImageCount) {
        if (OrlixHostRootImageCount >= ORLIX_HOST_MAX_ROOT_IMAGES) {
            os_unfair_lock_unlock(&OrlixHostRootImagesLock);
            return -1;
        }
        OrlixHostRootImageCount++;
    }
    OrlixHostRootImages[target_index] = *root_image;
    os_unfair_lock_unlock(&OrlixHostRootImagesLock);
    return 0;
}

__attribute__((visibility("default"))) int orlix_host_resources_register_root_image(
    const char *identifier,
    const char *initrd_bundle_name,
    const char *initrd_bundle_extension,
    const char *initrd_resource,
    const char *base_block_resource,
    const char *state_block_resource,
    unsigned int base_block_device,
    unsigned int state_block_device,
    unsigned long long state_block_minimum_bytes)
{
    struct OrlixHostRootImage root_image = { 0 };

    if (OrlixHostCopyRequiredString(root_image.identifier,
                                    sizeof(root_image.identifier),
                                    identifier) != 0 ||
        OrlixHostCopyOptionalString(root_image.initrd_bundle_name,
                                    sizeof(root_image.initrd_bundle_name),
                                    initrd_bundle_name) != 0 ||
        OrlixHostCopyOptionalString(root_image.initrd_bundle_extension,
                                    sizeof(root_image.initrd_bundle_extension),
                                    initrd_bundle_extension) != 0 ||
        OrlixHostCopyRequiredResource(root_image.initrd_resource,
                                      sizeof(root_image.initrd_resource),
                                      initrd_resource) != 0 ||
        OrlixHostCopyRequiredResource(root_image.base_block_resource,
                                      sizeof(root_image.base_block_resource),
                                      base_block_resource) != 0 ||
        OrlixHostCopyRequiredResource(root_image.state_block_resource,
                                      sizeof(root_image.state_block_resource),
                                      state_block_resource) != 0) {
        return -1;
    }
    root_image.base_block_device = base_block_device;
    root_image.state_block_device = state_block_device;
    root_image.state_block_minimum_bytes = state_block_minimum_bytes;
    return OrlixHostRegisterRootImage(&root_image);
}

__attribute__((visibility("default"))) int orlix_host_resources_register_root_image_files(
    const char *identifier,
    const char *initrd_bundle_name,
    const char *initrd_bundle_extension,
    const char *initrd_resource,
    const char *base_block_path,
    const char *state_block_path,
    unsigned int base_block_device,
    unsigned int state_block_device,
    unsigned long long state_block_minimum_bytes)
{
    struct OrlixHostRootImage root_image = { 0 };

    if (OrlixHostCopyRequiredString(root_image.identifier,
                                    sizeof(root_image.identifier),
                                    identifier) != 0 ||
        OrlixHostCopyOptionalString(root_image.initrd_bundle_name,
                                    sizeof(root_image.initrd_bundle_name),
                                    initrd_bundle_name) != 0 ||
        OrlixHostCopyOptionalString(root_image.initrd_bundle_extension,
                                    sizeof(root_image.initrd_bundle_extension),
                                    initrd_bundle_extension) != 0 ||
        OrlixHostCopyRequiredResource(root_image.initrd_resource,
                                      sizeof(root_image.initrd_resource),
                                      initrd_resource) != 0 ||
        OrlixHostCopyRequiredBlockFilePath(root_image.base_block_resource,
                                           sizeof(root_image.base_block_resource),
                                           base_block_path) != 0 ||
        OrlixHostCopyRequiredBlockFilePath(root_image.state_block_resource,
                                           sizeof(root_image.state_block_resource),
                                           state_block_path) != 0) {
        return -1;
    }
    root_image.block_images_are_files = 1;
    root_image.base_block_device = base_block_device;
    root_image.state_block_device = state_block_device;
    root_image.state_block_minimum_bytes = state_block_minimum_bytes;
    return OrlixHostRegisterRootImage(&root_image);
}

static int OrlixHostCopyPayloadRootPath(char *path, size_t path_size)
{
    size_t length;

    if (!path || path_size == 0) {
        return -1;
    }

    os_unfair_lock_lock(&OrlixHostPayloadRootLock);
    length = strlen(OrlixHostPayloadRootPath);
    if (length == 0 || length >= path_size) {
        os_unfair_lock_unlock(&OrlixHostPayloadRootLock);
        return -1;
    }
    memcpy(path, OrlixHostPayloadRootPath, length + 1);
    os_unfair_lock_unlock(&OrlixHostPayloadRootLock);
    return 0;
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
                                               const char *bundle_extension,
                                               const char *resource,
                                               char *path,
                                               size_t path_size)
{
    char bundle_root[PATH_MAX];
    int written;

    if (!bundle_name || !bundle_extension || !resource || !path ||
        path_size == 0) {
        return -1;
    }
    if (OrlixHostCopyMainBundleResourceRootPath(bundle_name,
                                               bundle_extension,
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

    if (device >= ORLIX_HOST_MAX_BLOCK_DEVICES || !path) {
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
    return device < ORLIX_HOST_MAX_BLOCK_DEVICES &&
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

static int OrlixHostCopyStateBlockPath(const char *identifier,
                                       char *path,
                                       size_t path_size)
{
    const char *home = getenv("HOME");
    char library[PATH_MAX];
    char application_support[PATH_MAX];
    char orlix[PATH_MAX];
    char state_name[192];
    size_t index;
    size_t out_index = 0;
    int written;

    if (!home || home[0] == '\0' || !identifier ||
        identifier[0] == '\0' || !path || path_size == 0) {
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

    for (index = 0; identifier[index] != '\0' &&
                    out_index + 1 < sizeof(state_name);
         index++) {
        char c = identifier[index];
        if ((c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') ||
            c == '-' || c == '_') {
            state_name[out_index++] = c;
        } else {
            state_name[out_index++] = '-';
        }
    }
    state_name[out_index] = '\0';
    if (identifier[index] != '\0' || state_name[0] == '\0') {
        return -1;
    }

    written = snprintf(path, path_size, "%s/%s-state.img", orlix, state_name);
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
                                         unsigned long long minimum_bytes,
                                         unsigned long long *size)
{
    unsigned long long target_size;
    struct stat state;
    int fd;

    if (!path || !template_path || minimum_bytes == 0 || !size) {
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
    if (target_size < minimum_bytes) {
        target_size = minimum_bytes;
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

static int OrlixHostEnsureExistingStateBlockFile(const char *path,
                                                 unsigned long long minimum_bytes,
                                                 unsigned long long *size)
{
    unsigned long long target_size;
    struct stat state;
    int fd;

    if (!path || minimum_bytes == 0 || !size) {
        return -1;
    }

    fd = open(path, O_RDWR);
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
    if (target_size < minimum_bytes) {
        target_size = minimum_bytes;
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
    struct OrlixHostRootImage root_image;
    char path[PATH_MAX];

    if (!loaded ||
        OrlixHostCopyRootImageForIdentifier(identifier, &root_image) != 0) {
        return -1;
    }

    loaded->data = 0;
    loaded->size = 0;
    if (root_image.initrd_bundle_name[0] != '\0') {
        if (OrlixHostCopyTestBundleResourcePath(root_image.initrd_bundle_name,
                                                root_image.initrd_bundle_extension,
                                                root_image.initrd_resource,
                                                path,
                                                sizeof(path)) != 0) {
            return -1;
        }
        return OrlixHostReadResourceFile(path, loaded);
    }

    if (OrlixHostCopyPayloadResourcePath(root_image.initrd_resource,
                                         path,
                                         sizeof(path)) != 0) {
        return -1;
    }
    return OrlixHostReadResourceFile(path, loaded);
}

__attribute__((visibility("hidden"))) int OrlixHostSelectBootBlockImages(
    const char *identifier)
{
    struct OrlixHostRootImage root_image;
    unsigned long long base_size = 0;
    unsigned long long state_size = 0;
    char base_path[PATH_MAX];
    char state_template_path[PATH_MAX];
    char state_path[PATH_MAX];

    OrlixHostClearSelectedBlockImages();

    if (OrlixHostCopyRootImageForIdentifier(identifier, &root_image) != 0) {
        return -1;
    }
    if (root_image.block_images_are_files) {
        if (OrlixHostCopySelectedBlockPath(root_image.base_block_device,
                                           root_image.base_block_resource) != 0 ||
            OrlixHostCopySelectedBlockPath(root_image.state_block_device,
                                           root_image.state_block_resource) != 0 ||
            OrlixHostResourceFileSize(root_image.base_block_resource,
                                      &base_size) != 0 ||
            OrlixHostEnsureExistingStateBlockFile(
                root_image.state_block_resource,
                root_image.state_block_minimum_bytes,
                &state_size) != 0) {
            OrlixHostClearSelectedBlockImages();
            return -1;
        }
        OrlixHostSelectedBlockBytes[root_image.base_block_device] = base_size;
        OrlixHostSelectedBlockBytes[root_image.state_block_device] = state_size;
        OrlixHostSelectedBlockWritable[root_image.state_block_device] = 1;
        return 0;
    }
    if (OrlixHostCopyPayloadResourcePath(root_image.base_block_resource,
                                         base_path,
                                         sizeof(base_path)) != 0 ||
        OrlixHostCopyPayloadResourcePath(root_image.state_block_resource,
                                         state_template_path,
                                         sizeof(state_template_path)) != 0) {
        return -1;
    }
    if (OrlixHostResourceFileSize(base_path, &base_size) != 0) {
        return -1;
    }
    if (OrlixHostCopyStateBlockPath(identifier, state_path, sizeof(state_path)) != 0) {
        return -1;
    }
    if (OrlixHostEnsureStateBlockFile(state_path,
                                      state_template_path,
                                      root_image.state_block_minimum_bytes,
                                      &state_size) != 0) {
        return -1;
    }
    if (OrlixHostCopySelectedBlockPath(root_image.base_block_device,
                                       base_path) != 0 ||
        OrlixHostCopySelectedBlockPath(root_image.state_block_device,
                                       state_path) != 0) {
        OrlixHostClearSelectedBlockImages();
        return -1;
    }

    OrlixHostSelectedBlockBytes[root_image.base_block_device] = base_size;
    OrlixHostSelectedBlockBytes[root_image.state_block_device] = state_size;
    OrlixHostSelectedBlockWritable[root_image.state_block_device] = 1;
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

__attribute__((visibility("hidden"))) int orlix_host_block_flush(
    unsigned int device)
{
    unsigned long active_tls;
    int fd;
    int result = -1;

    if (!OrlixHostBlockDeviceIsSelected(device)) {
        return -1;
    }

    active_tls = OrlixHostEnterHostTls();
    fd = open(OrlixHostSelectedBlockPaths[device],
              OrlixHostSelectedBlockWritable[device] ? O_RDWR : O_RDONLY);
    if (fd < 0) {
        goto out;
    }

    result = fsync(fd) == 0 ? 0 : -1;
    if (close(fd) != 0) {
        result = -1;
    }

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

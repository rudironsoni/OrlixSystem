#include "vfs.h"
#include "fdtable.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <linux/capability.h>
#include <linux/xattr.h>

static bool xattr_is_security_capability(const char *name) {
    return name && strcmp(name, "security.capability") == 0;
}

static int xattr_decode_capability(const void *value, size_t size, uint64_t *permitted,
                                   uint64_t *inheritable, bool *effective) {
    const struct vfs_cap_data *cap;
    uint32_t revision;
    uint64_t permitted_mask;
    uint64_t inheritable_mask;

    if (!value || !permitted || !inheritable || !effective) {
        return -EFAULT;
    }
    if (size != XATTR_CAPS_SZ_2 && size != XATTR_CAPS_SZ_3) {
        return -EINVAL;
    }

    cap = (const struct vfs_cap_data *)value;
    revision = cap->magic_etc & VFS_CAP_REVISION_MASK;
    if (revision != VFS_CAP_REVISION_2 && revision != VFS_CAP_REVISION_3) {
        return -EINVAL;
    }

    permitted_mask = (uint64_t)cap->data[0].permitted |
                     ((uint64_t)cap->data[1].permitted << 32);
    inheritable_mask = (uint64_t)cap->data[0].inheritable |
                       ((uint64_t)cap->data[1].inheritable << 32);

    *permitted = permitted_mask;
    *inheritable = inheritable_mask;
    *effective = (cap->magic_etc & VFS_CAP_FLAGS_EFFECTIVE) != 0;
    return 0;
}

static size_t xattr_encode_capability(uint64_t permitted, uint64_t inheritable, bool effective,
                                      void *value, size_t size) {
    struct vfs_ns_cap_data cap;

    memset(&cap, 0, sizeof(cap));
    cap.magic_etc = VFS_CAP_REVISION_3 | (effective ? VFS_CAP_FLAGS_EFFECTIVE : 0);
    cap.data[0].permitted = (uint32_t)(permitted & 0xffffffffU);
    cap.data[1].permitted = (uint32_t)(permitted >> 32);
    cap.data[0].inheritable = (uint32_t)(inheritable & 0xffffffffU);
    cap.data[1].inheritable = (uint32_t)(inheritable >> 32);
    cap.rootid = 0;

    if (value) {
        if (size < sizeof(cap)) {
            errno = ERANGE;
            return 0;
        }
        memcpy(value, &cap, sizeof(cap));
    }
    return sizeof(cap);
}

int setxattr_impl(const char *path, const char *name, const void *value, size_t size, int flags) {
    uint64_t permitted;
    uint64_t inheritable;
    bool effective;
    bool exists;
    int ret;

    if (!path || !name || (!value && size > 0)) {
        errno = EFAULT;
        return -1;
    }
    if ((flags & ~(XATTR_CREATE | XATTR_REPLACE)) != 0 ||
        ((flags & XATTR_CREATE) != 0 && (flags & XATTR_REPLACE) != 0)) {
        errno = EINVAL;
        return -1;
    }
    if (!xattr_is_security_capability(name)) {
        errno = ENOTSUP;
        return -1;
    }

    ret = vfs_get_file_capabilities(path, &permitted, &inheritable, &effective);
    exists = ret == 0;
    if ((flags & XATTR_CREATE) != 0 && exists) {
        errno = EEXIST;
        return -1;
    }
    if ((flags & XATTR_REPLACE) != 0 && !exists) {
        errno = ENODATA;
        return -1;
    }

    ret = xattr_decode_capability(value, size, &permitted, &inheritable, &effective);
    if (ret != 0) {
        errno = -ret;
        return -1;
    }
    return vfs_set_file_capabilities(path, permitted, inheritable, effective);
}

long getxattr_impl(const char *path, const char *name, void *value, size_t size) {
    uint64_t permitted;
    uint64_t inheritable;
    bool effective;
    size_t encoded_size;
    int ret;

    if (!path || !name) {
        errno = EFAULT;
        return -1;
    }
    if (!xattr_is_security_capability(name)) {
        errno = ENODATA;
        return -1;
    }
    ret = vfs_get_file_capabilities(path, &permitted, &inheritable, &effective);
    if (ret != 0) {
        errno = -ret;
        return -1;
    }
    encoded_size = xattr_encode_capability(permitted, inheritable, effective, value, size);
    if (encoded_size == 0 && value) {
        return -1;
    }
    return (long)encoded_size;
}

int removexattr_impl(const char *path, const char *name) {
    if (!path || !name) {
        errno = EFAULT;
        return -1;
    }
    if (!xattr_is_security_capability(name)) {
        errno = ENODATA;
        return -1;
    }
    return vfs_remove_file_capabilities(path);
}

static int xattr_path_from_fd(int fd, char *path, size_t path_len) {
    fd_entry_t *entry;
    int ret;

    entry = get_fd_entry_impl(fd);
    if (!entry) {
        errno = EBADF;
        return -1;
    }
    ret = get_fd_path_impl(entry, path, path_len);
    put_fd_entry_impl(entry);
    return ret;
}

int fsetxattr_impl(int fd, const char *name, const void *value, size_t size, int flags) {
    char path[MAX_PATH];

    if (xattr_path_from_fd(fd, path, sizeof(path)) != 0) {
        return -1;
    }
    return setxattr_impl(path, name, value, size, flags);
}

long fgetxattr_impl(int fd, const char *name, void *value, size_t size) {
    char path[MAX_PATH];

    if (xattr_path_from_fd(fd, path, sizeof(path)) != 0) {
        return -1;
    }
    return getxattr_impl(path, name, value, size);
}

int fremovexattr_impl(int fd, const char *name) {
    char path[MAX_PATH];

    if (xattr_path_from_fd(fd, path, sizeof(path)) != 0) {
        return -1;
    }
    return removexattr_impl(path, name);
}

__attribute__((visibility("default"))) int setxattr(const char *path, const char *name,
                                                     const void *value, size_t size, int flags) {
    return setxattr_impl(path, name, value, size, flags);
}

__attribute__((visibility("default"))) int lsetxattr(const char *path, const char *name,
                                                      const void *value, size_t size, int flags) {
    return setxattr_impl(path, name, value, size, flags);
}

__attribute__((visibility("default"))) int fsetxattr(int fd, const char *name,
                                                     const void *value, size_t size, int flags) {
    return fsetxattr_impl(fd, name, value, size, flags);
}

__attribute__((visibility("default"))) long getxattr(const char *path, const char *name,
                                                     void *value, size_t size) {
    return getxattr_impl(path, name, value, size);
}

__attribute__((visibility("default"))) long lgetxattr(const char *path, const char *name,
                                                      void *value, size_t size) {
    return getxattr_impl(path, name, value, size);
}

__attribute__((visibility("default"))) long fgetxattr(int fd, const char *name,
                                                      void *value, size_t size) {
    return fgetxattr_impl(fd, name, value, size);
}

__attribute__((visibility("default"))) int removexattr(const char *path, const char *name) {
    return removexattr_impl(path, name);
}

__attribute__((visibility("default"))) int lremovexattr(const char *path, const char *name) {
    return removexattr_impl(path, name);
}

__attribute__((visibility("default"))) int fremovexattr(int fd, const char *name) {
    return fremovexattr_impl(fd, name);
}

#include "vfs.h"
#include "fdtable.h"

#include <stdbool.h>
#include <stdint.h>

#include <linux/errno.h>
#include <linux/string.h>
#include <uapi/linux/capability.h>
#include <uapi/linux/xattr.h>

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
            return 0;
        }
        memcpy(value, &cap, sizeof(cap));
    }
    return sizeof(cap);
}

static int setxattr_impl_follow(const char *path, const char *name, const void *value, size_t size,
                                int flags, int follow_final_symlink) {
    uint64_t permitted;
    uint64_t inheritable;
    bool effective;
    bool exists;
    int ret;

    if (!path || !name || (!value && size > 0)) {
        return -EFAULT;
    }
    if ((flags & ~(XATTR_CREATE | XATTR_REPLACE)) != 0 ||
        ((flags & XATTR_CREATE) != 0 && (flags & XATTR_REPLACE) != 0)) {
        return -EINVAL;
    }
    if (!xattr_is_security_capability(name)) {
        ret = vfs_set_user_xattr_follow(path, name, value, size, flags, follow_final_symlink);
        return ret;
    }

    ret = vfs_get_file_capabilities_follow(path, &permitted, &inheritable, &effective,
                                           follow_final_symlink);
    exists = ret == 0;
    if ((flags & XATTR_CREATE) != 0 && exists) {
        return -EEXIST;
    }
    if ((flags & XATTR_REPLACE) != 0 && !exists) {
        return -ENODATA;
    }

    ret = xattr_decode_capability(value, size, &permitted, &inheritable, &effective);
    if (ret != 0) {
        return ret;
    }
    return vfs_set_file_capabilities_follow(path, permitted, inheritable, effective,
                                            follow_final_symlink);
}

int setxattr_impl(const char *path, const char *name, const void *value, size_t size, int flags) {
    return setxattr_impl_follow(path, name, value, size, flags, 1);
}

int lsetxattr_impl(const char *path, const char *name, const void *value, size_t size, int flags) {
    return setxattr_impl_follow(path, name, value, size, flags, 0);
}

static long getxattr_impl_follow(const char *path, const char *name, void *value, size_t size,
                                 int follow_final_symlink) {
    uint64_t permitted;
    uint64_t inheritable;
    bool effective;
    size_t encoded_size;
    int ret;

    if (!path || !name || (!value && size > 0)) {
        return -EFAULT;
    }
    if (!xattr_is_security_capability(name)) {
        return vfs_get_user_xattr_follow(path, name, value, size, follow_final_symlink);
    }
    ret = vfs_get_file_capabilities_follow(path, &permitted, &inheritable, &effective,
                                           follow_final_symlink);
    if (ret != 0) {
        return ret;
    }
    encoded_size = xattr_encode_capability(permitted, inheritable, effective, value, size);
    if (encoded_size == 0 && value) {
        return -ERANGE;
    }
    return (long)encoded_size;
}

long getxattr_impl(const char *path, const char *name, void *value, size_t size) {
    return getxattr_impl_follow(path, name, value, size, 1);
}

long lgetxattr_impl(const char *path, const char *name, void *value, size_t size) {
    return getxattr_impl_follow(path, name, value, size, 0);
}

static int removexattr_impl_follow(const char *path, const char *name, int follow_final_symlink) {
    if (!path || !name) {
        return -EFAULT;
    }
    if (!xattr_is_security_capability(name)) {
        return vfs_remove_user_xattr_follow(path, name, follow_final_symlink);
    }
    return vfs_remove_file_capabilities_follow(path, follow_final_symlink);
}

int removexattr_impl(const char *path, const char *name) {
    return removexattr_impl_follow(path, name, 1);
}

int lremovexattr_impl(const char *path, const char *name) {
    return removexattr_impl_follow(path, name, 0);
}

static long listxattr_impl_follow(const char *path, char *list, size_t size,
                                  int follow_final_symlink) {
    long ret;

    if (!path || (!list && size > 0)) {
        return -EFAULT;
    }
    ret = vfs_list_xattr_follow(path, list, size, follow_final_symlink);
    return ret;
}

long listxattr_impl(const char *path, char *list, size_t size) {
    return listxattr_impl_follow(path, list, size, 1);
}

long llistxattr_impl(const char *path, char *list, size_t size) {
    return listxattr_impl_follow(path, list, size, 0);
}

static int xattr_path_from_fd(int fd, char *path, size_t path_len) {
    fd_entry_t *entry;
    int ret;

    entry = get_fd_entry_impl(fd);
    if (!entry) {
        return -EBADF;
    }
    ret = get_fd_path_impl(entry, path, path_len);
    put_fd_entry_impl(entry);
    return ret;
}

int fsetxattr_impl(int fd, const char *name, const void *value, size_t size, int flags) {
    char path[MAX_PATH];
    int ret;

    ret = xattr_path_from_fd(fd, path, sizeof(path));
    if (ret != 0) {
        return ret;
    }
    return setxattr_impl(path, name, value, size, flags);
}

long fgetxattr_impl(int fd, const char *name, void *value, size_t size) {
    char path[MAX_PATH];
    int ret;

    ret = xattr_path_from_fd(fd, path, sizeof(path));
    if (ret != 0) {
        return ret;
    }
    return getxattr_impl(path, name, value, size);
}

int fremovexattr_impl(int fd, const char *name) {
    char path[MAX_PATH];
    int ret;

    ret = xattr_path_from_fd(fd, path, sizeof(path));
    if (ret != 0) {
        return ret;
    }
    return removexattr_impl(path, name);
}

long flistxattr_impl(int fd, char *list, size_t size) {
    char path[MAX_PATH];
    int ret;

    ret = xattr_path_from_fd(fd, path, sizeof(path));
    if (ret != 0) {
        return ret;
    }
    return listxattr_impl(path, list, size);
}

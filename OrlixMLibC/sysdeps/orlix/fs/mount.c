#include <errno.h>
#include <stddef.h>

#include <linux/mount.h>

extern int mount_impl(const char *source, const char *target,
                      const char *filesystemtype, unsigned long mountflags,
                      const void *data);
extern int umount_impl(const char *target);
extern int umount2_impl(const char *target, int flags);
extern int vfs_mount_setattr(int dirfd, const char *pathname, unsigned int flags,
                             const struct mount_attr *attr, size_t size);
extern int vfs_open_tree(int dirfd, const char *pathname, unsigned int flags);
extern int vfs_move_mount(int from_dirfd, const char *from_pathname, int to_dirfd,
                          const char *to_pathname, unsigned int flags);
extern int vfs_pivot_root(const char *new_root, const char *put_old);

static int wrap_int_result(int ret) {
    if (ret < 0) {
        errno = -ret;
        return -1;
    }
    return ret;
}

__attribute__((visibility("default"))) int mount(const char *source,
                                                 const char *target,
                                                 const char *filesystemtype,
                                                 unsigned long mountflags,
                                                 const void *data) {
    return wrap_int_result(mount_impl(source, target, filesystemtype, mountflags, data));
}

__attribute__((visibility("default"))) int umount(const char *target) {
    return wrap_int_result(umount_impl(target));
}

__attribute__((visibility("default"))) int umount2(const char *target, int flags) {
    return wrap_int_result(umount2_impl(target, flags));
}

__attribute__((visibility("default"))) int mount_setattr(int dirfd, const char *pathname,
                                                         unsigned int flags,
                                                         struct mount_attr *attr,
                                                         size_t size) {
    return wrap_int_result(vfs_mount_setattr(dirfd, pathname, flags, attr, size));
}

__attribute__((visibility("default"))) int open_tree(int dirfd, const char *pathname,
                                                     unsigned int flags) {
    return wrap_int_result(vfs_open_tree(dirfd, pathname, flags));
}

__attribute__((visibility("default"))) int move_mount(int from_dirfd, const char *from_pathname,
                                                      int to_dirfd, const char *to_pathname,
                                                      unsigned int flags) {
    return wrap_int_result(vfs_move_mount(from_dirfd, from_pathname, to_dirfd, to_pathname, flags));
}

__attribute__((visibility("default"))) int pivot_root(const char *new_root, const char *put_old) {
    return wrap_int_result(vfs_pivot_root(new_root, put_old));
}

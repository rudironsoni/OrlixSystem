/* fs/namei.c
 * Linux-shaped path operations
 *
 * This file contains only Linux-owner logic. All host operations
 * are delegated through the exported OrlixHostAdapter fs seam.
 */

#include <uapi/linux/capability.h>
#include <uapi/linux/fcntl.h>
#include <uapi/linux/stat.h>
#include <uapi/asm/stat.h>
#include <linux/errno.h>
#include <linux/string.h>

#include "internal/fs/namei.h"
#include "kernel/cgroup.h"

/* Standard file descriptors - local definitions to avoid Darwin <unistd.h> */
#ifndef STDERR_FILENO
#define STDERR_FILENO 2
#endif

#ifndef X_OK
#define X_OK 1
#endif

#include "vfs.h"
#include "private/fs/vfs_state.h"
#include "fdtable.h"
#include "../private/kernel/task_state.h"
#include "../kernel/task.h"
#include "../kernel/cred.h"

static int directory_validate_path(const char *path) {
    if (path == NULL) {
        return -EFAULT;
    }

    if (path[0] == '\0') {
        return -ENOENT;
    }

    return 0;
}

static int rename_translate_path_at(int dirfd, const char *path, char *translated_path,
                                    size_t translated_path_len) {
    char resolved_path[MAX_PATH];
    int ret;

    if (path == NULL) {
        return -EFAULT;
    }

    if (path[0] == '\0') {
        return -ENOENT;
    }

    ret = vfs_resolve_virtual_path_at_follow(dirfd, path, resolved_path, sizeof(resolved_path), false);
    if (ret != 0) {
        return ret;
    }

    ret = vfs_translate_path(resolved_path, translated_path, translated_path_len);
    if (ret != 0) {
        return ret;
    }

    return 0;
}

static int rename_resolve_virtual_path_at(int dirfd, const char *path, char *resolved_path,
                                          size_t resolved_path_len) {
    int ret;

    if (path == NULL) {
        return -EFAULT;
    }

    if (path[0] == '\0') {
        return -ENOENT;
    }

    ret = vfs_resolve_virtual_path_at_follow(dirfd, path, resolved_path, resolved_path_len, false);
    if (ret != 0) {
        return ret;
    }

    return 0;
}

static int rename_validate_same_route(const char *old_virtual_path, const char *new_virtual_path) {
    enum vfs_route_identity old_route;
    enum vfs_route_identity new_route;
    enum vfs_backing_class old_class;
    enum vfs_backing_class new_class;
    bool old_reversible;
    bool new_reversible;

    if (vfs_describe_route_for_path(old_virtual_path, &old_route, &old_class, &old_reversible) != 0 ||
        vfs_describe_route_for_path(new_virtual_path, &new_route, &new_class, &new_reversible) != 0) {
        return -EXDEV;
    }

    if (old_class == VFS_BACKING_SYNTHETIC || old_class == VFS_BACKING_EXTERNAL ||
        new_class == VFS_BACKING_SYNTHETIC || new_class == VFS_BACKING_EXTERNAL) {
        return -EXDEV;
    }

    if (!old_reversible || !new_reversible) {
        return -EXDEV;
    }

    if (old_route != new_route) {
        return -EXDEV;
    }

    return 0;
}

static int rename_apply_backing_operation(const char *old_virtual_path, const char *new_virtual_path,
                                       const char *old_backing_path, const char *new_backing_path,
                                       unsigned int backing_flags) {
    int ret = rename_validate_same_route(old_virtual_path, new_virtual_path);
    if (ret != 0) {
        return ret;
    }

    ret = backing_rename_with_flags(AT_FDCWD, old_backing_path, AT_FDCWD, new_backing_path, backing_flags);
    if (ret != 0) {
        return ret;
    }

    return 0;
}

static int rename_apply_backing_exchange(const char *old_virtual_path, const char *new_virtual_path,
                                      const char *old_backing_path, const char *new_backing_path) {
    int ret = rename_validate_same_route(old_virtual_path, new_virtual_path);
    if (ret != 0) {
        return ret;
    }

    ret = backing_rename_exchange(old_backing_path, new_backing_path);
    if (ret != 0) {
        return ret;
    }

    return 0;
}

static int rename_lstat_backing_entry(const char *backing_path, struct stat *st) {
    int ret;

    ret = backing_lstat(backing_path, st);
    return ret;
}

static int rename_lstat_optional_backing_entry(const char *backing_path, struct stat *st, bool *exists) {
    int ret;

    ret = backing_lstat(backing_path, st);
    if (ret == 0) {
        *exists = true;
        return 0;
    }
    if (ret != -ENOENT) {
        return ret;
    }
    *exists = false;
    return 0;
}

static int rename_validate_target_shape(const char *old_virtual_path, const char *new_virtual_path,
                                        const char *old_backing_path, const char *new_backing_path,
                                        unsigned int flags) {
    struct stat old_st;
    struct stat new_st;
    bool new_exists = false;
    bool old_is_dir;
    bool new_is_dir;
    int empty;

    empty = rename_lstat_backing_entry(old_backing_path, &old_st);
    if (empty != 0) {
        return empty;
    }
    empty = rename_lstat_optional_backing_entry(new_backing_path, &new_st, &new_exists);
    if (empty != 0) {
        return empty;
    }

    if ((flags & AT_RENAME_EXCHANGE) != 0) {
        if (!new_exists) {
            return -ENOENT;
        }
        return 0;
    }

    if ((flags & AT_RENAME_NOREPLACE) != 0 && new_exists) {
        return -EEXIST;
    }
    if (!new_exists || strcmp(old_virtual_path, new_virtual_path) == 0) {
        return 0;
    }

    old_is_dir = S_ISDIR(old_st.st_mode);
    new_is_dir = S_ISDIR(new_st.st_mode);
    if (old_is_dir && !new_is_dir) {
        return -ENOTDIR;
    }
    if (!old_is_dir && new_is_dir) {
        return -EISDIR;
    }
    if (old_is_dir && new_is_dir) {
        empty = backing_directory_is_empty(new_backing_path);
        if (empty < 0) {
            return empty;
        }
        if (empty == 0) {
            return -ENOTEMPTY;
        }
    }
    return 0;
}

int renameat2_impl(int olddirfd, const char *oldpath, int newdirfd, const char *newpath,
                   unsigned int flags) {
    char resolved_old[MAX_PATH];
    char resolved_new[MAX_PATH];
    char translated_old[MAX_PATH];
    char translated_new[MAX_PATH];
    unsigned int backing_flags = 0;
    int ret;

    if (oldpath == NULL || newpath == NULL) {
        return -EFAULT;
    }

    if (flags & ~(AT_RENAME_NOREPLACE | AT_RENAME_EXCHANGE | AT_RENAME_WHITEOUT)) {
        return -EINVAL;
    }

    if ((flags & AT_RENAME_WHITEOUT) != 0) {
        return -EOPNOTSUPP;
    }

    if ((flags & AT_RENAME_EXCHANGE) != 0 && (flags & AT_RENAME_NOREPLACE) != 0) {
        return -EINVAL;
    }

    ret = rename_resolve_virtual_path_at(olddirfd, oldpath, resolved_old, sizeof(resolved_old));
    if (ret != 0) {
        return ret;
    }

    ret = rename_resolve_virtual_path_at(newdirfd, newpath, resolved_new, sizeof(resolved_new));
    if (ret != 0) {
        return ret;
    }

    ret = rename_translate_path_at(olddirfd, oldpath, translated_old, sizeof(translated_old));
    if (ret != 0) {
        return ret;
    }

    ret = rename_translate_path_at(newdirfd, newpath, translated_new, sizeof(translated_new));
    if (ret != 0) {
        return ret;
    }

    if ((flags & AT_RENAME_NOREPLACE) != 0) {
        backing_flags |= AT_RENAME_NOREPLACE;
    }

    ret = rename_validate_target_shape(resolved_old, resolved_new, translated_old, translated_new, flags);
    if (ret != 0) {
        return ret;
    }

    ret = vfs_check_parent_mutation_permission(resolved_old);
    if (ret != 0) {
        return ret;
    }
    ret = vfs_check_parent_mutation_permission(resolved_new);
    if (ret != 0) {
        return ret;
    }
    ret = vfs_check_sticky_rename_permission(resolved_old, resolved_new);
    if (ret != 0) {
        return ret;
    }

    if ((flags & AT_RENAME_EXCHANGE) != 0) {
        ret = rename_apply_backing_exchange(resolved_old, resolved_new, translated_old, translated_new);
        if (ret == 0) {
            fdtable_exchange_paths_impl(resolved_old, resolved_new);
            task_exchange_vma_backing_paths_impl(resolved_old, resolved_new);
            vfs_exchange_path_metadata(resolved_old, resolved_new);
        }
        return ret;
    }

    ret = rename_apply_backing_operation(resolved_old, resolved_new, translated_old, translated_new, backing_flags);
    if (ret == 0) {
        fdtable_rename_path_impl(resolved_old, resolved_new);
        task_rename_vma_backing_path_impl(resolved_old, resolved_new);
        vfs_rename_path_metadata(resolved_old, resolved_new);
    }
    return ret;
}

static int directory_translate_task_path(const char *path, char *translated_path,
                                         size_t translated_path_len,
                                         struct task **task_out) {
    struct task *task;
    char resolved_path[MAX_PATH];
    int ret;

    task = task_current();
    if (!task) {
        return -ESRCH;
    }

    ret = vfs_resolve_virtual_path_task_follow(path, resolved_path, sizeof(resolved_path),
                                               task->fs, true);
    if (ret != 0) {
        return ret;
    }

    ret = vfs_translate_path(resolved_path, translated_path, translated_path_len);
    if (ret != 0) {
        return ret;
    }

    if (task_out) {
        *task_out = task;
    }

    return 0;
}

static bool directory_virtual_path_contains(const char *root, const char *path) {
    size_t root_len;

    if (!root || !path) {
        return false;
    }

    if (strcmp(root, "/") == 0) {
        return path[0] == '/';
    }

    root_len = strlen(root);
    return strcmp(root, path) == 0 || (strncmp(path, root, root_len) == 0 && path[root_len] == '/');
}

int chdir_impl(const char *path) {
    struct task *task;
    char translated_path[MAX_PATH];
    char resolved_virtual[MAX_PATH];
    int ret;

    ret = directory_validate_path(path);
    if (ret != 0) {
        return ret;
    }

    ret = directory_translate_task_path(path, translated_path, sizeof(translated_path), &task);
    if (ret != 0) {
        return ret;
    }

    struct stat st;
    ret = backing_stat(translated_path, &st);
    if (ret != 0) {
        return ret;
    }

    if (!S_ISDIR(st.st_mode)) {
        return -ENOTDIR;
    }

    ret = backing_access(translated_path, X_OK);
    if (ret != 0) {
        return ret;
    }

    if (task->fs) {
        ret = vfs_resolve_virtual_path_task_follow(path, resolved_virtual,
                                                   sizeof(resolved_virtual), task->fs, true);
        if (ret != 0) {
            return ret;
        }
        fs_set_pwd(task->fs, resolved_virtual);
    }

    return 0;
}

int fchdir_impl(int fd) {
    fd_entry_t *entry;
    struct task *task;
    char fd_path[MAX_PATH];
    int ret;

    if (fd < 0 || fd >= NR_OPEN_DEFAULT || fd <= STDERR_FILENO) {
        return -EBADF;
    }

    task = task_current();
    if (!task || !task->fs) {
        return -ESRCH;
    }

    entry = get_fd_entry_impl(fd);
    if (!entry) {
        return -EBADF;
    }
    if (!get_fd_is_dir_impl(entry)) {
        put_fd_entry_impl(entry);
        return -ENOTDIR;
    }
    ret = get_fd_path_impl(entry, fd_path, sizeof(fd_path));
    put_fd_entry_impl(entry);
    if (ret != 0) {
        return ret;
    }

    ret = fs_set_pwd(task->fs, fd_path);
    if (ret != 0) {
        return ret;
    }

    return 0;
}

int getcwd_impl(char *buf, size_t size) {
    struct task *task;
    char virtual_path[MAX_PATH];
    int ret;

    if (size == 0) {
        return -EINVAL;
    }

    if (buf == NULL) {
        return -EINVAL;
    }

    task = task_current();
    if (!task) {
        return -ESRCH;
    }

    ret = vfs_getcwd_path_task(task->fs, virtual_path, sizeof(virtual_path));
    if (ret != 0) {
        return ret;
    }

    const size_t selected_len = strlen(virtual_path);
    if (selected_len >= size) {
        return -ERANGE;
    }

    memcpy(buf, virtual_path, selected_len + 1);
    return 0;
}

int mkdirat_impl(int dirfd, const char *pathname, mode_t mode) {
    char translated_path[MAX_PATH];
    char resolved_path[MAX_PATH];
    int ret;

    ret = directory_validate_path(pathname);
    if (ret != 0) {
        return ret;
    }

    ret = vfs_resolve_virtual_path_at(dirfd, pathname, resolved_path, sizeof(resolved_path));
    if (ret == 0) {
        char mounted_path[MAX_PATH];
        ret = vfs_apply_mounts_to_path(resolved_path, mounted_path, sizeof(mounted_path));
        if (ret == 0 && strncmp(mounted_path, "/sys/fs/cgroup/", 15) == 0) {
            ret = cgroupfs_mkdir(mounted_path);
            if (ret != 0) {
                return ret;
            }
            return 0;
        }
    }

    ret = vfs_resolve_virtual_path_at_follow(dirfd, pathname, resolved_path, sizeof(resolved_path), false);
    if (ret != 0) {
        return ret;
    }
    {
        char mounted_path[MAX_PATH];
        ret = vfs_apply_mounts_to_path(resolved_path, mounted_path, sizeof(mounted_path));
        if (ret == 0 && strncmp(mounted_path, "/sys/fs/cgroup/", 15) == 0) {
            ret = cgroupfs_mkdir(mounted_path);
            if (ret != 0) {
                return ret;
            }
            return 0;
        }
    }
    if (strncmp(resolved_path, "/sys/fs/cgroup/", 15) == 0) {
        ret = cgroupfs_mkdir(resolved_path);
        if (ret != 0) {
            return ret;
        }
        return 0;
    }
    ret = vfs_check_parent_mutation_permission(resolved_path);
    if (ret != 0) {
        return ret;
    }

    ret = vfs_translate_path(resolved_path, translated_path, sizeof(translated_path));
    if (ret != 0) {
        return ret;
    }

    ret = backing_mkdir(translated_path, mode);
    if (ret == 0) {
        vfs_record_created_path(resolved_path, S_IFDIR | mode);
    }
    return ret;
}

int mkdir_impl(const char *pathname, mode_t mode) {
    return mkdirat_impl(AT_FDCWD, pathname, mode);
}

int unlinkat_impl(int dirfd, const char *pathname, int flags) {
    char translated_path[MAX_PATH];
    char resolved_path[MAX_PATH];
    struct stat st;
    bool remove_dir;
    int ret;

    if ((flags & ~AT_REMOVEDIR) != 0) {
        return -EINVAL;
    }

    ret = directory_validate_path(pathname);
    if (ret != 0) {
        return ret;
    }

    remove_dir = (flags & AT_REMOVEDIR) != 0;

    ret = vfs_resolve_virtual_path_at_follow(dirfd, pathname, resolved_path, sizeof(resolved_path), false);
    if (ret != 0) {
        return ret;
    }
    if (remove_dir) {
        char mounted_path[MAX_PATH];
        ret = vfs_apply_mounts_to_path(resolved_path, mounted_path, sizeof(mounted_path));
        if (ret == 0 && strncmp(mounted_path, "/sys/fs/cgroup/", 15) == 0) {
            ret = cgroupfs_rmdir(mounted_path);
            if (ret != 0) {
                return ret;
            }
            return 0;
        }
    }
    ret = vfs_check_parent_mutation_permission(resolved_path);
    if (ret != 0) {
        return ret;
    }

    ret = vfs_translate_path(resolved_path, translated_path, sizeof(translated_path));
    if (ret != 0) {
        return ret;
    }

    ret = backing_lstat(translated_path, &st);
    if (ret != 0) {
        return ret;
    }

    if (remove_dir && !S_ISDIR(st.st_mode)) {
        return -ENOTDIR;
    }
    if (!remove_dir && S_ISDIR(st.st_mode)) {
        return -EISDIR;
    }
    ret = vfs_check_sticky_unlink_permission(resolved_path);
    if (ret != 0) {
        return ret;
    }

    ret = remove_dir ? backing_rmdir(translated_path) : backing_unlink(translated_path);
    if (ret == 0) {
        fdtable_mark_path_deleted_impl(resolved_path);
        vfs_forget_path_metadata(resolved_path);
    }
    return ret;
}

int rmdir_impl(const char *pathname) {
    return unlinkat_impl(AT_FDCWD, pathname, AT_REMOVEDIR);
}

int unlink_impl(const char *pathname) {
    return unlinkat_impl(AT_FDCWD, pathname, 0);
}

int linkat_impl(int olddirfd, const char *oldpath, int newdirfd, const char *newpath, int flags) {
    char resolved_old[MAX_PATH];
    char resolved_new[MAX_PATH];
    char translated_old[MAX_PATH];
    char translated_new[MAX_PATH];
    struct stat st;
    int ret;

    if (oldpath == NULL || newpath == NULL) {
        return -EFAULT;
    }

    if (oldpath[0] == '\0' || newpath[0] == '\0') {
        return -ENOENT;
    }

    if (flags & ~AT_SYMLINK_FOLLOW) {
        return -EINVAL;
    }

    ret = vfs_resolve_virtual_path_at_follow(olddirfd, oldpath, resolved_old,
                                             sizeof(resolved_old), (flags & AT_SYMLINK_FOLLOW) != 0);
    if (ret != 0) {
        return ret;
    }

    ret = vfs_resolve_virtual_path_at_follow(newdirfd, newpath, resolved_new, sizeof(resolved_new), false);
    if (ret != 0) {
        return ret;
    }
    ret = vfs_check_parent_mutation_permission(resolved_new);
    if (ret != 0) {
        return ret;
    }

    ret = vfs_translate_path(resolved_old, translated_old, sizeof(translated_old));
    if (ret != 0) {
        return ret;
    }
    ret = vfs_translate_path(resolved_new, translated_new, sizeof(translated_new));
    if (ret != 0) {
        return ret;
    }

    if ((flags & AT_SYMLINK_FOLLOW) != 0) {
        ret = backing_stat(translated_old, &st);
    } else {
        ret = backing_lstat(translated_old, &st);
    }
    if (ret != 0) {
        return ret;
    }

    if (S_ISDIR(st.st_mode)) {
        return -EPERM;
    }

    ret = backing_lstat(translated_new, &st);
    if (ret == 0) {
        return -EEXIST;
    }
    if (ret != -ENOENT) {
        return ret;
    }

    ret = backing_linkat(translated_old, translated_new, (flags & AT_SYMLINK_FOLLOW) != 0);
    if (ret == 0) {
        vfs_link_path_metadata(resolved_old, resolved_new);
    }
    return ret;
}

int link_impl(const char *oldpath, const char *newpath) {
    return linkat_impl(AT_FDCWD, oldpath, AT_FDCWD, newpath, 0);
}

int symlinkat_impl(const char *target, int newdirfd, const char *linkpath) {
    char resolved_link[MAX_PATH];
    char translated_link[MAX_PATH];
    struct stat st;
    int ret;

    if (target == NULL || linkpath == NULL) {
        return -EFAULT;
    }

    if (linkpath[0] == '\0') {
        return -ENOENT;
    }

    ret = vfs_resolve_virtual_path_at_follow(newdirfd, linkpath, resolved_link,
                                             sizeof(resolved_link), false);
    if (ret != 0) {
        return ret;
    }
    ret = vfs_check_parent_mutation_permission(resolved_link);
    if (ret != 0) {
        return ret;
    }
    ret = vfs_translate_path(resolved_link, translated_link, sizeof(translated_link));
    if (ret != 0) {
        return ret;
    }

    ret = backing_lstat(translated_link, &st);
    if (ret == 0) {
        return -EEXIST;
    }
    if (ret != -ENOENT) {
        return ret;
    }

    ret = backing_symlink(target, translated_link);
    if (ret == 0) {
        vfs_record_created_path(resolved_link, S_IFLNK | 0777);
    }
    return ret;
}

int symlink_impl(const char *target, const char *linkpath) {
    return symlinkat_impl(target, AT_FDCWD, linkpath);
}

ssize_t readlinkat_impl(int dirfd, const char *pathname, char *buf, size_t bufsiz) {
    char translated_path[MAX_PATH];
    char resolved_virtual[MAX_PATH];
    int ret;

    if (pathname == NULL || buf == NULL) {
        return -EFAULT;
    }

    if (pathname[0] == '\0') {
        return -ENOENT;
    }

    if (bufsiz == 0) {
        return -EINVAL;
    }

    ret = vfs_resolve_virtual_path_at_follow(dirfd, pathname, resolved_virtual,
                                             sizeof(resolved_virtual), false);
    if (ret != 0) {
        return ret;
    }

    if (vfs_path_is_linux_route(resolved_virtual)) {
        proc_self_path_class_t proc_class = vfs_classify_proc_self_path(resolved_virtual);
        if (proc_class == PROC_SELF_FD_LINK) {
            char link_target[MAX_PATH];
            ret = vfs_proc_self_fd_link_target(resolved_virtual, link_target, sizeof(link_target));
            if (ret != 0) {
                return ret;
            }
            size_t target_len = strlen(link_target);
            if (target_len > bufsiz) {
                target_len = bufsiz;
            }
            memcpy(buf, link_target, target_len);
            return (ssize_t)target_len;
        } else if (proc_class == PROC_SELF_CWD_LINK) {
            char link_target[MAX_PATH];
            ret = vfs_proc_task_cwd_target(vfs_proc_target_pid_for_path(resolved_virtual),
                                           link_target, sizeof(link_target));
            if (ret != 0) {
                return ret;
            }
            size_t target_len = strlen(link_target);
            if (target_len > bufsiz) {
                target_len = bufsiz;
            }
            memcpy(buf, link_target, target_len);
            return (ssize_t)target_len;
        } else if (proc_class == PROC_SELF_EXE_LINK) {
            char link_target[MAX_PATH];
            ret = vfs_proc_task_exe_target(vfs_proc_target_pid_for_path(resolved_virtual),
                                           link_target, sizeof(link_target));
            if (ret != 0) {
                return ret;
            }
            size_t target_len = strlen(link_target);
            if (target_len > bufsiz) {
                target_len = bufsiz;
            }
            memcpy(buf, link_target, target_len);
            return (ssize_t)target_len;
        } else if (proc_class == PROC_SELF_NS_LINK) {
            char link_target[MAX_PATH];
            ret = vfs_proc_self_ns_link_target(resolved_virtual, link_target, sizeof(link_target));
            if (ret != 0) {
                return ret;
            }
            size_t target_len = strlen(link_target);
            if (target_len > bufsiz) {
                target_len = bufsiz;
            }
            memcpy(buf, link_target, target_len);
            return (ssize_t)target_len;
        }
    }

    ret = vfs_translate_path(resolved_virtual, translated_path, sizeof(translated_path));
    if (ret != 0) {
        return ret;
    }

    struct stat path_stat;
    ret = backing_lstat(translated_path, &path_stat);
    if (ret != 0) {
        return ret;
    }

    if (!S_ISLNK(path_stat.st_mode)) {
        return -EINVAL;
    }

    ret = backing_readlink(translated_path, buf, bufsiz);
    return ret;
}

ssize_t readlink_impl(const char *pathname, char *buf, size_t bufsiz) {
    return readlinkat_impl(AT_FDCWD, pathname, buf, bufsiz);
}

int chroot_impl(const char *path) {
    struct task *task;
    char translated_path[MAX_PATH];
    char resolved_virtual[MAX_PATH];
    char current_pwd[MAX_PATH];
    struct stat st;
    int ret;

    ret = directory_validate_path(path);
    if (ret != 0) {
        return ret;
    }

    if (!cred_has_cap(cred_current(), CAP_SYS_CHROOT)) {
        return -EPERM;
    }

    ret = directory_translate_task_path(path, translated_path, sizeof(translated_path), &task);
    if (ret != 0) {
        return ret;
    }

    if (!task->fs) {
        return -ESRCH;
    }

    ret = backing_stat(translated_path, &st);
    if (ret != 0) {
        return ret;
    }

    if (!S_ISDIR(st.st_mode)) {
        return -ENOTDIR;
    }

    ret = backing_access(translated_path, X_OK);
    if (ret != 0) {
        return ret;
    }

    ret = vfs_resolve_virtual_path_task_follow(path, resolved_virtual, sizeof(resolved_virtual),
                                               task->fs, true);
    if (ret != 0) {
        return ret;
    }

    memcpy(current_pwd, task->fs->pwd_path, sizeof(current_pwd));
    ret = fs_set_root(task->fs, resolved_virtual);
    if (ret != 0) {
        return ret;
    }

    if (!directory_virtual_path_contains(resolved_virtual, current_pwd)) {
        ret = fs_set_pwd(task->fs, resolved_virtual);
        if (ret != 0) {
            return ret;
        }
    }

    return 0;
}

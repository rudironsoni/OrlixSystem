/* fs/namei.c
 * Linux-shaped path operations
 *
 * This file contains only Linux-owner logic. All host operations
 * are delegated through narrow seams in internal/ios/fs/path_host.h
 */

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Linux UAPI headers for ABI constants and types */
#include <linux/capability.h>
#include <linux/fcntl.h>
#include <linux/stat.h>
#include <asm-generic/stat.h>

#include "IXLandHostAdapter/fs/path_host.h"
#include "kernel/cgroup.h"

/* Standard file descriptors - local definitions to avoid Darwin <unistd.h> */
#ifndef STDERR_FILENO
#define STDERR_FILENO 2
#endif

#ifndef X_OK
#define X_OK 1
#endif

#include "vfs.h"
#include "fdtable.h"
#include "../kernel/task.h"
#include "../kernel/cred_internal.h"

static int directory_validate_path(const char *path) {
    if (path == NULL) {
        errno = EFAULT;
        return -1;
    }

    if (path[0] == '\0') {
        errno = ENOENT;
        return -1;
    }

    return 0;
}

/* Get current task - forward declaration */
struct task_struct *get_current(void);

static int rename_translate_path_at(int dirfd, const char *path, char *translated_path,
                                    size_t translated_path_len) {
    char resolved_path[MAX_PATH];
    int ret;

    if (path == NULL) {
        errno = EFAULT;
        return -1;
    }

    if (path[0] == '\0') {
        errno = ENOENT;
        return -1;
    }

    ret = vfs_resolve_virtual_path_at_follow(dirfd, path, resolved_path, sizeof(resolved_path), false);
    if (ret != 0) {
        errno = -ret;
        return -1;
    }

    ret = vfs_translate_path(resolved_path, translated_path, translated_path_len);
    if (ret != 0) {
        errno = -ret;
        return -1;
    }

    return 0;
}

static int rename_resolve_virtual_path_at(int dirfd, const char *path, char *resolved_path,
                                          size_t resolved_path_len) {
    int ret;

    if (path == NULL) {
        errno = EFAULT;
        return -1;
    }

    if (path[0] == '\0') {
        errno = ENOENT;
        return -1;
    }

    ret = vfs_resolve_virtual_path_at_follow(dirfd, path, resolved_path, resolved_path_len, false);
    if (ret != 0) {
        errno = -ret;
        return -1;
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
        errno = EXDEV;
        return -1;
    }

    if (old_class == VFS_BACKING_SYNTHETIC || old_class == VFS_BACKING_EXTERNAL ||
        new_class == VFS_BACKING_SYNTHETIC || new_class == VFS_BACKING_EXTERNAL) {
        errno = EXDEV;
        return -1;
    }

    if (!old_reversible || !new_reversible) {
        errno = EXDEV;
        return -1;
    }

    if (old_route != new_route) {
        errno = EXDEV;
        return -1;
    }

    return 0;
}

static int rename_apply_host_operation(const char *old_virtual_path, const char *new_virtual_path,
                                       const char *old_host_path, const char *new_host_path,
                                       unsigned int host_flags) {
    if (rename_validate_same_route(old_virtual_path, new_virtual_path) != 0) {
        return -1;
    }

    if (host_renameatx_np_impl(AT_FDCWD, old_host_path, AT_FDCWD, new_host_path, host_flags) != 0) {
        return -1;
    }

    return 0;
}

static int rename_apply_host_exchange(const char *old_virtual_path, const char *new_virtual_path,
                                      const char *old_host_path, const char *new_host_path) {
    if (rename_validate_same_route(old_virtual_path, new_virtual_path) != 0) {
        return -1;
    }

    if (host_rename_exchange_impl(old_host_path, new_host_path) != 0) {
        return -1;
    }

    return 0;
}

static int rename_lstat_host_entry(const char *host_path, struct linux_stat *st) {
    int ret;

    ret = host_lstat_impl(host_path, st);
    if (ret != 0) {
        errno = -ret;
        return -1;
    }
    return 0;
}

static int rename_lstat_optional_host_entry(const char *host_path, struct linux_stat *st, bool *exists) {
    int ret;

    ret = host_lstat_impl(host_path, st);
    if (ret == 0) {
        *exists = true;
        return 0;
    }
    if (ret != -ENOENT) {
        errno = -ret;
        return -1;
    }
    *exists = false;
    return 0;
}

static int rename_validate_target_shape(const char *old_virtual_path, const char *new_virtual_path,
                                        const char *old_host_path, const char *new_host_path,
                                        unsigned int flags) {
    struct linux_stat old_st;
    struct linux_stat new_st;
    bool new_exists = false;
    bool old_is_dir;
    bool new_is_dir;
    int empty;

    if (rename_lstat_host_entry(old_host_path, &old_st) != 0) {
        return -1;
    }
    if (rename_lstat_optional_host_entry(new_host_path, &new_st, &new_exists) != 0) {
        return -1;
    }

    if ((flags & AT_RENAME_EXCHANGE) != 0) {
        if (!new_exists) {
            errno = ENOENT;
            return -1;
        }
        return 0;
    }

    if ((flags & AT_RENAME_NOREPLACE) != 0 && new_exists) {
        errno = EEXIST;
        return -1;
    }
    if (!new_exists || strcmp(old_virtual_path, new_virtual_path) == 0) {
        return 0;
    }

    old_is_dir = S_ISDIR(old_st.st_mode);
    new_is_dir = S_ISDIR(new_st.st_mode);
    if (old_is_dir && !new_is_dir) {
        errno = ENOTDIR;
        return -1;
    }
    if (!old_is_dir && new_is_dir) {
        errno = EISDIR;
        return -1;
    }
    if (old_is_dir && new_is_dir) {
        empty = host_directory_is_empty_impl(new_host_path);
        if (empty < 0) {
            errno = -empty;
            return -1;
        }
        if (empty == 0) {
            errno = ENOTEMPTY;
            return -1;
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
    unsigned int host_flags = 0;
    int ret;

    if (oldpath == NULL || newpath == NULL) {
        errno = EFAULT;
        return -1;
    }

    if (flags & ~(AT_RENAME_NOREPLACE | AT_RENAME_EXCHANGE | AT_RENAME_WHITEOUT)) {
        errno = EINVAL;
        return -1;
    }

    if ((flags & AT_RENAME_WHITEOUT) != 0) {
        errno = EOPNOTSUPP;
        return -1;
    }

    if ((flags & AT_RENAME_EXCHANGE) != 0 && (flags & AT_RENAME_NOREPLACE) != 0) {
        errno = EINVAL;
        return -1;
    }

    if (rename_resolve_virtual_path_at(olddirfd, oldpath, resolved_old, sizeof(resolved_old)) != 0) {
        return -1;
    }

    if (rename_resolve_virtual_path_at(newdirfd, newpath, resolved_new, sizeof(resolved_new)) != 0) {
        return -1;
    }

    if (rename_translate_path_at(olddirfd, oldpath, translated_old, sizeof(translated_old)) != 0) {
        return -1;
    }

    if (rename_translate_path_at(newdirfd, newpath, translated_new, sizeof(translated_new)) != 0) {
        return -1;
    }

    if ((flags & AT_RENAME_NOREPLACE) != 0) {
        host_flags |= RENAME_EXCL;
    }

    if (rename_validate_target_shape(resolved_old, resolved_new, translated_old, translated_new, flags) != 0) {
        return -1;
    }

    ret = vfs_check_parent_mutation_permission(resolved_old);
    if (ret != 0) {
        errno = -ret;
        return -1;
    }
    ret = vfs_check_parent_mutation_permission(resolved_new);
    if (ret != 0) {
        errno = -ret;
        return -1;
    }
    ret = vfs_check_sticky_rename_permission(resolved_old, resolved_new);
    if (ret != 0) {
        errno = -ret;
        return -1;
    }

    if ((flags & AT_RENAME_EXCHANGE) != 0) {
        ret = rename_apply_host_exchange(resolved_old, resolved_new, translated_old, translated_new);
        if (ret == 0) {
            fdtable_exchange_paths_impl(resolved_old, resolved_new);
            task_exchange_vma_backing_paths_impl(resolved_old, resolved_new);
            vfs_exchange_path_metadata(resolved_old, resolved_new);
        }
        return ret;
    }

    ret = rename_apply_host_operation(resolved_old, resolved_new, translated_old, translated_new, host_flags);
    if (ret == 0) {
        fdtable_rename_path_impl(resolved_old, resolved_new);
        task_rename_vma_backing_path_impl(resolved_old, resolved_new);
        vfs_rename_path_metadata(resolved_old, resolved_new);
    }
    return ret;
}

static int directory_translate_task_path(const char *path, char *translated_path,
                                         size_t translated_path_len,
                                         struct task_struct **task_out) {
    struct task_struct *task;
    char resolved_path[MAX_PATH];
    int ret;

    task = get_current();
    if (!task) {
        errno = ESRCH;
        return -1;
    }

    ret = vfs_resolve_virtual_path_task_follow(path, resolved_path, sizeof(resolved_path),
                                               task->fs, true);
    if (ret != 0) {
        errno = -ret;
        return -1;
    }

    ret = vfs_translate_path(resolved_path, translated_path, translated_path_len);
    if (ret != 0) {
        errno = -ret;
        return -1;
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
    struct task_struct *task;
    char translated_path[MAX_PATH];
    char resolved_virtual[MAX_PATH];

    if (directory_validate_path(path) != 0) {
        return -1;
    }

    if (directory_translate_task_path(path, translated_path, sizeof(translated_path), &task) != 0) {
        return -1;
    }

    struct linux_stat st;
    if (host_stat_impl(translated_path, &st) != 0) {
        return -1;
    }

    if (!S_ISDIR(st.st_mode)) {
        errno = ENOTDIR;
        return -1;
    }

    if (host_access_impl(translated_path, X_OK) != 0) {
        errno = EACCES;
        return -1;
    }

    if (task->fs) {
        int ret = vfs_resolve_virtual_path_task_follow(path, resolved_virtual,
                                                       sizeof(resolved_virtual), task->fs, true);
        if (ret != 0) {
            errno = -ret;
            return -1;
        }
        fs_set_pwd(task->fs, resolved_virtual);
    }

    return 0;
}

int fchdir_impl(int fd) {
    fd_entry_t *entry;
    struct task_struct *task;
    char fd_path[MAX_PATH];
    int ret;

    if (fd < 0 || fd >= NR_OPEN_DEFAULT || fd <= STDERR_FILENO) {
        errno = EBADF;
        return -1;
    }

    task = get_current();
    if (!task || !task->fs) {
        errno = ESRCH;
        return -1;
    }

    entry = get_fd_entry_impl(fd);
    if (!entry) {
        errno = EBADF;
        return -1;
    }
    if (!get_fd_is_dir_impl(entry)) {
        put_fd_entry_impl(entry);
        errno = ENOTDIR;
        return -1;
    }
    ret = get_fd_path_impl(entry, fd_path, sizeof(fd_path));
    put_fd_entry_impl(entry);
    if (ret != 0) {
        return -1;
    }

    ret = fs_set_pwd(task->fs, fd_path);
    if (ret != 0) {
        errno = -ret;
        return -1;
    }

    return 0;
}

char *getcwd_impl(char *buf, size_t size) {
    struct task_struct *task;
    char virtual_path[MAX_PATH];
    int ret;

    if (size == 0) {
        errno = EINVAL;
        return NULL;
    }

    if (buf == NULL) {
        errno = EINVAL;
        return NULL;
    }

    task = get_current();
    if (!task) {
        errno = ESRCH;
        return NULL;
    }

    ret = vfs_getcwd_path_task(task->fs, virtual_path, sizeof(virtual_path));
    if (ret != 0) {
        errno = -ret;
        return NULL;
    }

    const size_t selected_len = strlen(virtual_path);
    if (selected_len >= size) {
        errno = ERANGE;
        return NULL;
    }

    memcpy(buf, virtual_path, selected_len + 1);
    return buf;
}

int mkdirat_impl(int dirfd, const char *pathname, mode_t mode) {
    char translated_path[MAX_PATH];
    char resolved_path[MAX_PATH];
    int ret;

    if (directory_validate_path(pathname) != 0) {
        return -1;
    }

    ret = vfs_resolve_virtual_path_at(dirfd, pathname, resolved_path, sizeof(resolved_path));
    if (ret == 0) {
        char mounted_path[MAX_PATH];
        ret = vfs_apply_mounts_to_path(resolved_path, mounted_path, sizeof(mounted_path));
        if (ret == 0 && strncmp(mounted_path, "/sys/fs/cgroup/", 15) == 0) {
            ret = cgroupfs_mkdir(mounted_path);
            if (ret != 0) {
                errno = -ret;
                return -1;
            }
            return 0;
        }
    }

    ret = vfs_resolve_virtual_path_at_follow(dirfd, pathname, resolved_path, sizeof(resolved_path), false);
    if (ret != 0) {
        errno = -ret;
        return -1;
    }
    {
        char mounted_path[MAX_PATH];
        ret = vfs_apply_mounts_to_path(resolved_path, mounted_path, sizeof(mounted_path));
        if (ret == 0 && strncmp(mounted_path, "/sys/fs/cgroup/", 15) == 0) {
            ret = cgroupfs_mkdir(mounted_path);
            if (ret != 0) {
                errno = -ret;
                return -1;
            }
            return 0;
        }
    }
    if (strncmp(resolved_path, "/sys/fs/cgroup/", 15) == 0) {
        ret = cgroupfs_mkdir(resolved_path);
        if (ret != 0) {
            errno = -ret;
            return -1;
        }
        return 0;
    }
    ret = vfs_check_parent_mutation_permission(resolved_path);
    if (ret != 0) {
        errno = -ret;
        return -1;
    }

    ret = vfs_translate_path(resolved_path, translated_path, sizeof(translated_path));
    if (ret != 0) {
        errno = -ret;
        return -1;
    }

    ret = host_mkdir_impl(translated_path, mode);
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
    struct linux_stat st;
    bool remove_dir;
    int ret;

    if ((flags & ~AT_REMOVEDIR) != 0) {
        errno = EINVAL;
        return -1;
    }

    if (directory_validate_path(pathname) != 0) {
        return -1;
    }

    remove_dir = (flags & AT_REMOVEDIR) != 0;

    ret = vfs_resolve_virtual_path_at_follow(dirfd, pathname, resolved_path, sizeof(resolved_path), false);
    if (ret != 0) {
        errno = -ret;
        return -1;
    }
    if (remove_dir) {
        char mounted_path[MAX_PATH];
        ret = vfs_apply_mounts_to_path(resolved_path, mounted_path, sizeof(mounted_path));
        if (ret == 0 && strncmp(mounted_path, "/sys/fs/cgroup/", 15) == 0) {
            ret = cgroupfs_rmdir(mounted_path);
            if (ret != 0) {
                errno = -ret;
                return -1;
            }
            return 0;
        }
    }
    ret = vfs_check_parent_mutation_permission(resolved_path);
    if (ret != 0) {
        errno = -ret;
        return -1;
    }

    ret = vfs_translate_path(resolved_path, translated_path, sizeof(translated_path));
    if (ret != 0) {
        errno = -ret;
        return -1;
    }

    if (host_lstat_impl(translated_path, &st) != 0) {
        return -1;
    }

    if (remove_dir && !S_ISDIR(st.st_mode)) {
        errno = ENOTDIR;
        return -1;
    }
    if (!remove_dir && S_ISDIR(st.st_mode)) {
        errno = EISDIR;
        return -1;
    }
    ret = vfs_check_sticky_unlink_permission(resolved_path);
    if (ret != 0) {
        errno = -ret;
        return -1;
    }

    ret = remove_dir ? host_rmdir_impl(translated_path) : host_unlink_impl(translated_path);
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

static int linkat_impl(int olddirfd, const char *oldpath, int newdirfd, const char *newpath, int flags) {
    char resolved_old[MAX_PATH];
    char resolved_new[MAX_PATH];
    char translated_old[MAX_PATH];
    char translated_new[MAX_PATH];
    struct linux_stat st;
    int ret;

    if (oldpath == NULL || newpath == NULL) {
        errno = EFAULT;
        return -1;
    }

    if (oldpath[0] == '\0' || newpath[0] == '\0') {
        errno = ENOENT;
        return -1;
    }

    if (flags & ~AT_SYMLINK_FOLLOW) {
        errno = EINVAL;
        return -1;
    }

    ret = vfs_resolve_virtual_path_at_follow(olddirfd, oldpath, resolved_old,
                                             sizeof(resolved_old), (flags & AT_SYMLINK_FOLLOW) != 0);
    if (ret != 0) {
        errno = -ret;
        return -1;
    }

    ret = vfs_resolve_virtual_path_at_follow(newdirfd, newpath, resolved_new, sizeof(resolved_new), false);
    if (ret != 0) {
        errno = -ret;
        return -1;
    }
    ret = vfs_check_parent_mutation_permission(resolved_new);
    if (ret != 0) {
        errno = -ret;
        return -1;
    }

    ret = vfs_translate_path(resolved_old, translated_old, sizeof(translated_old));
    if (ret != 0) {
        errno = -ret;
        return -1;
    }
    ret = vfs_translate_path(resolved_new, translated_new, sizeof(translated_new));
    if (ret != 0) {
        errno = -ret;
        return -1;
    }

    if ((flags & AT_SYMLINK_FOLLOW) != 0) {
        ret = host_stat_impl(translated_old, &st);
    } else {
        ret = host_lstat_impl(translated_old, &st);
    }
    if (ret != 0) {
        return -1;
    }

    if (S_ISDIR(st.st_mode)) {
        errno = EPERM;
        return -1;
    }

    if (host_lstat_impl(translated_new, &st) == 0) {
        errno = EEXIST;
        return -1;
    }
    if (errno != ENOENT) {
        return -1;
    }

    ret = host_linkat_impl(translated_old, translated_new, (flags & AT_SYMLINK_FOLLOW) != 0);
    if (ret == 0) {
        vfs_link_path_metadata(resolved_old, resolved_new);
    }
    return ret;
}

int link_impl(const char *oldpath, const char *newpath) {
    return linkat_impl(AT_FDCWD, oldpath, AT_FDCWD, newpath, 0);
}

static int symlinkat_impl(const char *target, int newdirfd, const char *linkpath) {
    char resolved_link[MAX_PATH];
    char translated_link[MAX_PATH];
    struct linux_stat st;
    int ret;

    if (target == NULL || linkpath == NULL) {
        errno = EFAULT;
        return -1;
    }

    if (linkpath[0] == '\0') {
        errno = ENOENT;
        return -1;
    }

    ret = vfs_resolve_virtual_path_at_follow(newdirfd, linkpath, resolved_link,
                                             sizeof(resolved_link), false);
    if (ret != 0) {
        errno = -ret;
        return -1;
    }
    ret = vfs_check_parent_mutation_permission(resolved_link);
    if (ret != 0) {
        errno = -ret;
        return -1;
    }
    ret = vfs_translate_path(resolved_link, translated_link, sizeof(translated_link));
    if (ret != 0) {
        errno = -ret;
        return -1;
    }

    if (host_lstat_impl(translated_link, &st) == 0) {
        errno = EEXIST;
        return -1;
    }
    if (errno != ENOENT) {
        return -1;
    }

    ret = host_symlink_impl(target, translated_link);
    if (ret == 0) {
        vfs_record_created_path(resolved_link, S_IFLNK | 0777);
    }
    return ret;
}

int symlink_impl(const char *target, const char *linkpath) {
    return symlinkat_impl(target, AT_FDCWD, linkpath);
}

static ssize_t readlinkat_impl(int dirfd, const char *pathname, char *buf, size_t bufsiz) {
    char translated_path[MAX_PATH];
    char resolved_virtual[MAX_PATH];
    int ret;

    if (pathname == NULL || buf == NULL) {
        errno = EFAULT;
        return -1;
    }

    if (pathname[0] == '\0') {
        errno = ENOENT;
        return -1;
    }

    if (bufsiz == 0) {
        errno = EINVAL;
        return -1;
    }

    ret = vfs_resolve_virtual_path_at_follow(dirfd, pathname, resolved_virtual,
                                             sizeof(resolved_virtual), false);
    if (ret != 0) {
        errno = -ret;
        return -1;
    }

    if (vfs_path_is_linux_route(resolved_virtual)) {
        proc_self_path_class_t proc_class = vfs_classify_proc_self_path(resolved_virtual);
        if (proc_class == PROC_SELF_FD_LINK) {
            char link_target[MAX_PATH];
            ret = vfs_proc_self_fd_link_target(resolved_virtual, link_target, sizeof(link_target));
            if (ret != 0) {
                errno = -ret;
                return -1;
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
                errno = -ret;
                return -1;
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
                errno = -ret;
                return -1;
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
                errno = -ret;
                return -1;
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
        errno = -ret;
        return -1;
    }

    struct linux_stat path_stat;
    if (host_lstat_impl(translated_path, &path_stat) != 0) {
        return -1;
    }

    if (!S_ISLNK(path_stat.st_mode)) {
        errno = EINVAL;
        return -1;
    }

    return host_readlink_impl(translated_path, buf, bufsiz);
}

ssize_t readlink_impl(const char *pathname, char *buf, size_t bufsiz) {
    return readlinkat_impl(AT_FDCWD, pathname, buf, bufsiz);
}

int chroot_impl(const char *path) {
    struct task_struct *task;
    char translated_path[MAX_PATH];
    char resolved_virtual[MAX_PATH];
    char current_pwd[MAX_PATH];
    struct linux_stat st;
    int ret;

    if (directory_validate_path(path) != 0) {
        return -1;
    }

    if (!cred_has_cap(get_current_cred(), CAP_SYS_CHROOT)) {
        errno = EPERM;
        return -1;
    }

    if (directory_translate_task_path(path, translated_path, sizeof(translated_path), &task) != 0) {
        return -1;
    }

    if (!task->fs) {
        errno = ESRCH;
        return -1;
    }

    if (host_stat_impl(translated_path, &st) != 0) {
        return -1;
    }

    if (!S_ISDIR(st.st_mode)) {
        errno = ENOTDIR;
        return -1;
    }

    if (host_access_impl(translated_path, X_OK) != 0) {
        errno = EACCES;
        return -1;
    }

    ret = vfs_resolve_virtual_path_task_follow(path, resolved_virtual, sizeof(resolved_virtual),
                                               task->fs, true);
    if (ret != 0) {
        errno = -ret;
        return -1;
    }

    memcpy(current_pwd, task->fs->pwd_path, sizeof(current_pwd));
    ret = fs_set_root(task->fs, resolved_virtual);
    if (ret != 0) {
        errno = -ret;
        return -1;
    }

    if (!directory_virtual_path_contains(resolved_virtual, current_pwd)) {
        ret = fs_set_pwd(task->fs, resolved_virtual);
        if (ret != 0) {
            errno = -ret;
            return -1;
        }
    }

    return 0;
}

__attribute__((visibility("default"))) int chdir(const char *path) {
  return chdir_impl(path);
}

__attribute__((visibility("default"))) int fchdir(int fd) {
  return fchdir_impl(fd);
}

__attribute__((visibility("default"))) char *getcwd(char *buf, size_t size) {
  return getcwd_impl(buf, size);
}

__attribute__((visibility("default"))) int mkdir(const char *pathname, mode_t mode) {
  return mkdir_impl(pathname, mode);
}

__attribute__((visibility("default"))) int mkdirat(int dirfd, const char *pathname, mode_t mode) {
  return mkdirat_impl(dirfd, pathname, mode);
}

__attribute__((visibility("default"))) int rmdir(const char *pathname) {
  return rmdir_impl(pathname);
}

__attribute__((visibility("default"))) int unlink(const char *pathname) {
  return unlink_impl(pathname);
}

__attribute__((visibility("default"))) int unlinkat(int dirfd, const char *pathname, int flags) {
  return unlinkat_impl(dirfd, pathname, flags);
}

__attribute__((visibility("default"))) int link(const char *oldpath, const char *newpath) {
  return link_impl(oldpath, newpath);
}

__attribute__((visibility("default"))) int linkat(int olddirfd, const char *oldpath, int newdirfd, const char *newpath, int flags) {
  return linkat_impl(olddirfd, oldpath, newdirfd, newpath, flags);
}

__attribute__((visibility("default"))) int symlink(const char *target, const char *linkpath) {
    return symlink_impl(target, linkpath);
}

__attribute__((visibility("default"))) int symlinkat(const char *target, int newdirfd, const char *linkpath) {
    return symlinkat_impl(target, newdirfd, linkpath);
}

__attribute__((visibility("default"))) ssize_t readlink(const char *pathname, char *buf, size_t bufsiz) {
    return readlink_impl(pathname, buf, bufsiz);
}

__attribute__((visibility("default"))) ssize_t readlinkat(int dirfd, const char *pathname, char *buf, size_t bufsiz) {
    return readlinkat_impl(dirfd, pathname, buf, bufsiz);
}

__attribute__((visibility("default"))) int rename(const char *oldpath, const char *newpath) {
    return renameat2_impl(AT_FDCWD, oldpath, AT_FDCWD, newpath, 0);
}

__attribute__((visibility("default"))) int renameat(int olddirfd, const char *oldpath, int newdirfd, const char *newpath) {
    return renameat2_impl(olddirfd, oldpath, newdirfd, newpath, 0);
}

__attribute__((visibility("default"))) int renameat2(int olddirfd, const char *oldpath, int newdirfd, const char *newpath, unsigned int flags) {
    return renameat2_impl(olddirfd, oldpath, newdirfd, newpath, flags);
}

__attribute__((visibility("default"))) int chroot(const char *path) {
    return chroot_impl(path);
}

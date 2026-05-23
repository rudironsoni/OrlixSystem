#include <linux/errno.h>
#include <linux/fcntl.h>
#include <linux/mount.h>
#include <linux/string.h>

#include "fdtable.h"
#include "private/fs/pty_state.h"
#include "vfs.h"
#include "private/fs/fdtable_state.h"
#include "private/fs/vfs_state.h"
#include "../private/kernel/cgroup_state.h"
#include "kernel/task.h"
#include "../private/kernel/task_state.h"

static int open_alloc_fd(void) {
    int fd = alloc_fd_impl();

    return fd < 0 ? -EMFILE : fd;
}

static bool open_path_is_nodev_blocked(const char *resolved_path) {
    return (vfs_mount_flags_for_path(resolved_path) & MNT_NODEV) != 0;
}

static synthetic_proc_file_t proc_self_file_for_class(proc_self_path_class_t proc_class) {
    switch (proc_class) {
    case PROC_SELF_CMDLINE_FILE:
        return SYNTHETIC_PROC_FILE_CMDLINE;
    case PROC_SELF_ENVIRON_FILE:
        return SYNTHETIC_PROC_FILE_ENVIRON;
    case PROC_SELF_COMM_FILE:
        return SYNTHETIC_PROC_FILE_COMM;
    case PROC_SELF_STAT_FILE:
        return SYNTHETIC_PROC_FILE_STAT;
    case PROC_SELF_STATM_FILE:
        return SYNTHETIC_PROC_FILE_STATM;
    case PROC_SELF_MAPS_FILE:
        return SYNTHETIC_PROC_FILE_MAPS;
    case PROC_SELF_SMAPS_FILE:
        return SYNTHETIC_PROC_FILE_SMAPS;
    case PROC_SELF_STATUS_FILE:
        return SYNTHETIC_PROC_FILE_STATUS;
    case PROC_SELF_CGROUP_FILE:
        return SYNTHETIC_PROC_FILE_CGROUP;
    case PROC_SELF_UID_MAP_FILE:
        return SYNTHETIC_PROC_FILE_UID_MAP;
    case PROC_SELF_GID_MAP_FILE:
        return SYNTHETIC_PROC_FILE_GID_MAP;
    case PROC_SELF_SETGROUPS_FILE:
        return SYNTHETIC_PROC_FILE_SETGROUPS;
    case PROC_SELF_MOUNTINFO_FILE:
        return SYNTHETIC_PROC_FILE_MOUNTINFO;
    case PROC_SELF_MOUNTS_FILE:
        return SYNTHETIC_PROC_FILE_MOUNTS;
    case PROC_ROOT_FILESYSTEMS_FILE:
        return SYNTHETIC_PROC_FILE_FILESYSTEMS;
    case PROC_ROOT_MEMINFO_FILE:
        return SYNTHETIC_PROC_FILE_MEMINFO;
    case PROC_ROOT_CPUINFO_FILE:
        return SYNTHETIC_PROC_FILE_CPUINFO;
    default:
        return SYNTHETIC_PROC_FILE_NONE;
    }
}

static bool open_path_is_numeric_proc_path(const char *resolved_path) {
    const char *cursor;

    if (!resolved_path || strncmp(resolved_path, "/proc/", 6) != 0 ||
        resolved_path[6] < '0' || resolved_path[6] > '9') {
        return false;
    }

    for (cursor = resolved_path + 6; *cursor != '\0'; cursor++) {
        if (*cursor == '/') {
            return true;
        }
        if (*cursor < '0' || *cursor > '9') {
            return false;
        }
    }

    return true;
}

static int try_open_proc_self_file(const char *resolved_path, int flags, mode_t mode,
                                   proc_self_path_class_t proc_class, int *out_fd) {
    synthetic_proc_file_t proc_file = proc_self_file_for_class(proc_class);
    int fd;

    if (!out_fd) {
        return -EINVAL;
    }
    *out_fd = -1;

    if (proc_file == SYNTHETIC_PROC_FILE_NONE && proc_class != PROC_SELF_FDINFO_FILE) {
        return 0;
    }

    if (((flags & O_ACCMODE) == O_WRONLY || (flags & O_ACCMODE) == O_RDWR) &&
        proc_file != SYNTHETIC_PROC_FILE_UID_MAP &&
        proc_file != SYNTHETIC_PROC_FILE_GID_MAP &&
        proc_file != SYNTHETIC_PROC_FILE_SETGROUPS) {
        return -EACCES;
    }

    if (proc_class == PROC_SELF_FDINFO_FILE) {
        int fd_num = vfs_proc_fd_num_for_path(resolved_path, "/fdinfo/");

        if (fd_num < 0) {
            return -ENOENT;
        }
        if (!vfs_proc_fd_exists_for_path(resolved_path, fd_num)) {
            return -ENOENT;
        }

        fd = open_alloc_fd();
        if (fd < 0) {
            return fd;
        }
        init_synthetic_proc_file_fd_entry_with_fdnum_for_pid_impl(fd, flags, mode, resolved_path,
                                                                  SYNTHETIC_PROC_FILE_FDINFO, fd_num,
                                                                  vfs_proc_target_pid_for_path(resolved_path));
        *out_fd = fd;
        return 1;
    }

    fd = open_alloc_fd();
    if (fd < 0) {
        return fd;
    }
    init_synthetic_proc_file_fd_entry_for_pid_impl(fd, flags, mode, resolved_path,
                                                   proc_file, vfs_proc_target_pid_for_path(resolved_path));
    *out_fd = fd;
    return 1;
}

static int try_open_cgroupfs(const char *resolved_path, int flags, mode_t mode, int *out_fd) {
    enum cgroupfs_node_type node_type;
    char mounted_path[MAX_PATH];
    char cgroup_path[MAX_PATH];
    int fd;

    if (!out_fd) {
        return -EINVAL;
    }
    *out_fd = -1;

    if (vfs_apply_mounts_to_path(resolved_path, mounted_path, sizeof(mounted_path)) != 0) {
        memcpy(mounted_path, resolved_path, strlen(resolved_path) + 1);
    }

    if (strcmp(mounted_path, "/sys/fs") == 0) {
        if ((flags & O_DIRECTORY) == 0 && (flags & O_PATH) != 0) {
            return 0;
        }
        fd = open_alloc_fd();
        if (fd < 0) {
            return fd;
        }
        init_synthetic_subdir_fd_entry_impl(fd, flags, mode, mounted_path, SYNTHETIC_DIR_CGROUPFS);
        *out_fd = fd;
        return 1;
    }

    if (cgroupfs_resolve_path(mounted_path, cgroup_path, sizeof(cgroup_path), &node_type) != 0) {
        if (strncmp(mounted_path, "/sys/fs/cgroup/", 15) == 0) {
            return -ENOENT;
        }
        return 0;
    }

    if (node_type == CGROUPFS_NODE_DIR) {
        if ((flags & O_DIRECTORY) == 0 && (flags & O_PATH) != 0) {
            return 0;
        }
        fd = open_alloc_fd();
        if (fd < 0) {
            return fd;
        }
        init_synthetic_subdir_fd_entry_impl(fd, flags, mode, mounted_path, SYNTHETIC_DIR_CGROUPFS);
        *out_fd = fd;
        return 1;
    }

    if ((flags & O_ACCMODE) == O_RDWR) {
        return -EACCES;
    }

    fd = open_alloc_fd();
    if (fd < 0) {
        return fd;
    }
    init_synthetic_cgroupfs_file_fd_entry_impl(fd, flags, mode, resolved_path, cgroup_path, node_type);
    *out_fd = fd;
    return 1;
}

static int try_open_special_dirs(const char *resolved_path, int flags, mode_t mode) {
    if ((strcmp(resolved_path, "/proc") == 0 || strcmp(resolved_path, "/sys") == 0) &&
        ((flags & O_DIRECTORY) != 0 || (flags & O_PATH) == 0)) {
        synthetic_dir_class_t dir_class = strcmp(resolved_path, "/proc") == 0 ?
            SYNTHETIC_DIR_PROC : SYNTHETIC_DIR_ROOT;
        int fd = open_alloc_fd();

        if (fd < 0) {
            return fd;
        }
        init_synthetic_subdir_fd_entry_impl(fd, flags, mode, resolved_path, dir_class);
        return fd;
    }

    if ((strcmp(resolved_path, "/dev") == 0 || strcmp(resolved_path, "/dev/pts") == 0) &&
        ((flags & O_DIRECTORY) != 0 || (flags & O_PATH) == 0)) {
        synthetic_dir_class_t dir_class = strcmp(resolved_path, "/dev") == 0 ?
            SYNTHETIC_DIR_DEV : SYNTHETIC_DIR_DEV_PTS;
        int fd = open_alloc_fd();

        if (fd < 0) {
            return fd;
        }
        init_synthetic_subdir_fd_entry_impl(fd, flags, mode, resolved_path, dir_class);
        return fd;
    }

    return 0;
}

static int try_open_proc_dirs(const char *resolved_path, int flags, mode_t mode) {
    proc_self_path_class_t proc_class = vfs_classify_proc_self_path(resolved_path);
    synthetic_dir_class_t dir_class = SYNTHETIC_DIR_PROC_SELF;
    int fd;
    int proc_ret;

    if ((proc_class == PROC_SELF_DIR || proc_class == PROC_SELF_FD_DIR ||
         proc_class == PROC_SELF_FDINFO_DIR || proc_class == PROC_SELF_NS_DIR ||
         proc_class == PROC_SELF_TASK_DIR || proc_class == PROC_SELF_THREAD_DIR) &&
        ((flags & O_DIRECTORY) != 0 || (flags & O_PATH) == 0)) {
        if (proc_class == PROC_SELF_FD_DIR) {
            dir_class = SYNTHETIC_DIR_PROC_SELF_FD;
        } else if (proc_class == PROC_SELF_FDINFO_DIR) {
            dir_class = SYNTHETIC_DIR_PROC_SELF_FDINFO;
        } else if (proc_class == PROC_SELF_NS_DIR) {
            dir_class = SYNTHETIC_DIR_PROC_SELF_NS;
        } else if (proc_class == PROC_SELF_TASK_DIR) {
            dir_class = SYNTHETIC_DIR_PROC_SELF_TASK;
        }

        fd = open_alloc_fd();
        if (fd < 0) {
            return fd;
        }
        init_synthetic_subdir_fd_entry_impl(fd, flags, mode, resolved_path, dir_class);
        return fd;
    }

    proc_ret = try_open_proc_self_file(resolved_path, flags, mode, proc_class, &fd);
    if (proc_ret != 0) {
        return proc_ret < 0 ? proc_ret : fd;
    }
    if (proc_class == PROC_SELF_NONE && open_path_is_numeric_proc_path(resolved_path)) {
        return -ENOENT;
    }

    return 0;
}

static int try_open_dev_node(const char *resolved_path, int flags, mode_t mode) {
    char mounted_dev_path[MAX_PATH];
    const char *dev_lookup_path = resolved_path;
    synthetic_dev_node_t dev_node;
    int fd;
    int ret;
    unsigned int pty_index;

    if (vfs_apply_mounts_to_path(resolved_path, mounted_dev_path, sizeof(mounted_dev_path)) == 0) {
        dev_lookup_path = mounted_dev_path;
    }
    dev_node = vfs_path_is_synthetic_dev_node(dev_lookup_path);

    if (dev_node == SYNTHETIC_DEV_PTMX) {
        if (open_path_is_nodev_blocked(resolved_path)) {
            return -EACCES;
        }
        ret = pty_allocate_pair_impl(&pty_index);
        if (ret < 0) {
            return ret;
        }
        fd = open_alloc_fd();
        if (fd < 0) {
            pty_close_end_impl(pty_index, true);
            return fd;
        }
        init_synthetic_pty_fd_entry_impl(fd, flags, mode, resolved_path, pty_index, true);
        return fd;
    }

    if (dev_node == SYNTHETIC_DEV_NONE) {
        return 0;
    }

    if (open_path_is_nodev_blocked(resolved_path)) {
        return -EACCES;
    }
    fd = open_alloc_fd();
    if (fd < 0) {
        return fd;
    }
    init_synthetic_dev_fd_entry_impl(fd, flags, mode, resolved_path, dev_node);
    return fd;
}

static int try_open_tty_path(const char *resolved_path, int flags, mode_t mode) {
    unsigned int pty_index;
    char mounted_tty_path[MAX_PATH];
    char tty_path[MAX_PATH];
    const char *tty_lookup_path = resolved_path;
    int fd;
    int ret;

    if (vfs_apply_mounts_to_path(resolved_path, mounted_tty_path, sizeof(mounted_tty_path)) == 0) {
        tty_lookup_path = mounted_tty_path;
    }

    if (strcmp(tty_lookup_path, "/dev/tty") == 0) {
        if (open_path_is_nodev_blocked(resolved_path)) {
            return -EACCES;
        }
        ret = pty_open_controlling_slave_impl(&pty_index);
        if (ret < 0) {
            return ret;
        }
        ret = pty_format_slave_path_impl(pty_index, tty_path, sizeof(tty_path));
        if (ret < 0) {
            pty_close_end_impl(pty_index, false);
            return ret;
        }
        fd = open_alloc_fd();
        if (fd < 0) {
            pty_close_end_impl(pty_index, false);
            return fd;
        }
        init_synthetic_pty_fd_entry_impl(fd, flags, mode, tty_path, pty_index, false);
        return fd;
    }

    ret = pty_open_slave_by_path_impl(tty_lookup_path, &pty_index);
    if (ret == -ENOENT) {
        return 0;
    }
    if (ret < 0) {
        return ret;
    }

    if (open_path_is_nodev_blocked(resolved_path)) {
        pty_close_end_impl(pty_index, false);
        return -EACCES;
    }
    fd = open_alloc_fd();
    if (fd < 0) {
        pty_close_end_impl(pty_index, false);
        return fd;
    }
    init_synthetic_pty_fd_entry_impl(fd, flags, mode, resolved_path, pty_index, false);
    return fd;
}

static int open_backing_path(const char *resolved_path, int flags, mode_t mode) {
    char translated_path[MAX_PATH];
    int fd;
    int real_fd;
    int ret;
    int path_exists;

    if (vfs_path_is_synthetic(resolved_path)) {
        return -EOPNOTSUPP;
    }

    ret = vfs_translate_path(resolved_path, translated_path, sizeof(translated_path));
    if (ret != 0) {
        return ret;
    }

    path_exists = vfs_path_exists(resolved_path);
    if (path_exists < 0) {
        return path_exists;
    }

    ret = vfs_check_open_permission(resolved_path, translated_path, flags);
    if (ret != 0) {
        return ret;
    }

    fd = open_alloc_fd();
    if (fd < 0) {
        return fd;
    }

    ret = vfs_open(translated_path, flags, mode, &real_fd);
    if (ret < 0) {
        free_fd_impl(fd);
        return ret;
    }

    if (path_exists == 0) {
        vfs_record_created_path(resolved_path, mode);
    }
    init_fd_entry_with_identity_impl(fd, real_fd, flags, mode, resolved_path,
                                     vfs_file_identity_for_path(resolved_path));
    return fd;
}

static int open_resolved_path(const char *resolved_path, int flags, mode_t mode) {
    int ret;
    int fd;

    ret = try_open_special_dirs(resolved_path, flags, mode);
    if (ret != 0) {
        return ret;
    }

    ret = try_open_proc_dirs(resolved_path, flags, mode);
    if (ret != 0) {
        return ret;
    }

    ret = try_open_cgroupfs(resolved_path, flags, mode, &fd);
    if (ret != 0) {
        return ret < 0 ? ret : fd;
    }

    ret = try_open_dev_node(resolved_path, flags, mode);
    if (ret != 0) {
        return ret;
    }

    ret = try_open_tty_path(resolved_path, flags, mode);
    if (ret != 0) {
        return ret;
    }

    return open_backing_path(resolved_path, flags, mode);
}

int open_impl(const char *pathname, int flags, mode_t mode) {
    char resolved_path[MAX_PATH];
    struct task *task;
    int ret;

    if (!pathname) {
        return -EFAULT;
    }

    task = task_current();
    ret = vfs_resolve_virtual_path_task_follow(pathname, resolved_path, sizeof(resolved_path),
                                               task ? task->fs : NULL, (flags & O_NOFOLLOW) == 0);
    if (ret != 0) {
        return ret;
    }

    return open_resolved_path(resolved_path, flags, mode);
}

int openat_impl(int dirfd, const char *pathname, int flags, mode_t mode) {
    char resolved_path[MAX_PATH];
    int ret;

    if (!pathname) {
        return -EFAULT;
    }

    ret = vfs_resolve_virtual_path_at_follow(dirfd, pathname, resolved_path, sizeof(resolved_path),
                                             (flags & O_NOFOLLOW) == 0);
    if (ret != 0) {
        return ret;
    }

    return open_resolved_path(resolved_path, flags, mode);
}

int creat_impl(const char *pathname, mode_t mode) {
    return open_impl(pathname, O_WRONLY | O_CREAT | O_TRUNC, mode);
}

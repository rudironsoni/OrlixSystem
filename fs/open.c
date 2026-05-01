#include <linux/fcntl.h>

#include <errno.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "fdtable.h"
#include "internal/ios/fs/sync.h"
#include "pty.h"
#include "vfs.h"

#define MAX_PATH 4096

static synthetic_proc_file_t proc_self_file_for_class(proc_self_path_class_t proc_class) {
    switch (proc_class) {
    case PROC_SELF_CMDLINE_FILE:
        return SYNTHETIC_PROC_FILE_CMDLINE;
    case PROC_SELF_COMM_FILE:
        return SYNTHETIC_PROC_FILE_COMM;
    case PROC_SELF_STAT_FILE:
        return SYNTHETIC_PROC_FILE_STAT;
    case PROC_SELF_STATM_FILE:
        return SYNTHETIC_PROC_FILE_STATM;
    case PROC_SELF_STATUS_FILE:
        return SYNTHETIC_PROC_FILE_STATUS;
    case PROC_SELF_MOUNTINFO_FILE:
        return SYNTHETIC_PROC_FILE_MOUNTINFO;
    case PROC_SELF_MOUNTS_FILE:
        return SYNTHETIC_PROC_FILE_MOUNTS;
    default:
        return SYNTHETIC_PROC_FILE_NONE;
    }
}

static int try_open_proc_self_file(const char *resolved_path, int flags, mode_t mode,
                                   proc_self_path_class_t proc_class, int *out_fd) {
    synthetic_proc_file_t proc_file = proc_self_file_for_class(proc_class);
    int fd;

    if (!out_fd) {
        errno = EINVAL;
        return -1;
    }
    *out_fd = -1;

    if (proc_file == SYNTHETIC_PROC_FILE_NONE && proc_class != PROC_SELF_FDINFO_FILE) {
        return 0;
    }

    if ((flags & O_ACCMODE) == O_WRONLY || (flags & O_ACCMODE) == O_RDWR) {
        errno = EACCES;
        return -1;
    }

    if (proc_class == PROC_SELF_FDINFO_FILE) {
        const char *fd_str = resolved_path + 18;
        char *endptr;
        long fd_num = strtol(fd_str, &endptr, 10);

        if (*endptr != '\0' || fd_num < 0 || fd_num >= NR_OPEN_DEFAULT) {
            errno = ENOENT;
            return -1;
        }
        if (!fdtable_is_used_impl((int)fd_num)) {
            errno = ENOENT;
            return -1;
        }

        fd = alloc_fd_impl();
        if (fd < 0) {
            return -1;
        }
        init_synthetic_proc_file_fd_entry_with_fdnum_impl(fd, flags, mode, resolved_path,
                                                          SYNTHETIC_PROC_FILE_FDINFO, (int)fd_num);
        *out_fd = fd;
        return 1;
    }

    fd = alloc_fd_impl();
    if (fd < 0) {
        return -1;
    }
    init_synthetic_proc_file_fd_entry_impl(fd, flags, mode, resolved_path, proc_file);
    *out_fd = fd;
    return 1;
}

int open_impl(const char *pathname, int flags, mode_t mode) {
    char translated_path[MAX_PATH];
    char resolved_path[MAX_PATH];
    int ret;

    if (!pathname) {
        errno = EFAULT;
        return -1;
    }

    ret = vfs_resolve_virtual_path_task_follow(pathname, resolved_path, sizeof(resolved_path),
                                               NULL, (flags & O_NOFOLLOW) == 0);
    if (ret != 0) {
        errno = -ret;
        return -1;
    }

    /* Always create synthetic fds for /proc, /sys, /dev */
    if ((strcmp(resolved_path, "/proc") == 0 || strcmp(resolved_path, "/sys") == 0) &&
        ((flags & O_DIRECTORY) != 0 || (flags & O_PATH) == 0)) {
        int fd = alloc_fd_impl();
        if (fd < 0) {
            return -1;
        }
        init_synthetic_fd_entry_impl(fd, flags, mode, resolved_path);
        return fd;
    }

    if ((strcmp(resolved_path, "/dev") == 0 || strcmp(resolved_path, "/dev/pts") == 0) &&
        ((flags & O_DIRECTORY) != 0 || (flags & O_PATH) == 0)) {
        int fd = alloc_fd_impl();
        if (fd < 0) {
            return -1;
        }
        synthetic_dir_class_t dir_class = strcmp(resolved_path, "/dev") == 0 ? SYNTHETIC_DIR_DEV : SYNTHETIC_DIR_DEV_PTS;
        init_synthetic_subdir_fd_entry_impl(fd, flags, mode, resolved_path, dir_class);
        return fd;
    }

    {
        proc_self_path_class_t proc_class = vfs_classify_proc_self_path(resolved_path);
        if ((proc_class == PROC_SELF_DIR || proc_class == PROC_SELF_FD_DIR || proc_class == PROC_SELF_FDINFO_DIR || proc_class == PROC_SELF_NS_DIR) && ((flags & O_DIRECTORY) != 0 || (flags & O_PATH) == 0)) {
            int fd = alloc_fd_impl();
            if (fd < 0) {
                return -1;
            }
            synthetic_dir_class_t dir_class = SYNTHETIC_DIR_PROC_SELF;
            if (proc_class == PROC_SELF_FD_DIR) {
                dir_class = SYNTHETIC_DIR_PROC_SELF_FD;
            } else if (proc_class == PROC_SELF_FDINFO_DIR) {
                dir_class = SYNTHETIC_DIR_PROC_SELF_FDINFO;
            } else if (proc_class == PROC_SELF_NS_DIR) {
                dir_class = SYNTHETIC_DIR_PROC_SELF_NS;
            }
            init_synthetic_subdir_fd_entry_impl(fd, flags, mode, resolved_path, dir_class);
            return fd;
        }
        {
            int proc_fd;
            int proc_ret = try_open_proc_self_file(resolved_path, flags, mode, proc_class, &proc_fd);
            if (proc_ret != 0) {
                return (proc_ret < 0) ? -1 : proc_fd;
            }
        }
    }

    {
        synthetic_dev_node_t dev_node = vfs_path_is_synthetic_dev_node(resolved_path);
        if (dev_node == SYNTHETIC_DEV_PTMX) {
            unsigned int pty_index;
            if (pty_allocate_pair_impl(&pty_index) != 0) {
                return -1;
            }
            int fd = alloc_fd_impl();
            if (fd < 0) {
                pty_close_end_impl(pty_index, true);
                return -1;
            }
            init_synthetic_pty_fd_entry_impl(fd, flags, mode, resolved_path, pty_index, true);
            return fd;
        }
        if (dev_node != SYNTHETIC_DEV_NONE) {
            int fd = alloc_fd_impl();
            if (fd < 0) {
                return -1;
            }
            init_synthetic_dev_fd_entry_impl(fd, flags, mode, resolved_path, dev_node);
            return fd;
        }
    }

    {
        unsigned int pty_index;
        if (strcmp(resolved_path, "/dev/tty") == 0) {
            char tty_path[MAX_PATH];
            if (pty_open_controlling_slave_impl(&pty_index) != 0) {
                return -1;
            }
            if (pty_format_slave_path_impl(pty_index, tty_path, sizeof(tty_path)) != 0) {
                pty_close_end_impl(pty_index, false);
                return -1;
            }
            int fd = alloc_fd_impl();
            if (fd < 0) {
                pty_close_end_impl(pty_index, false);
                return -1;
            }
            init_synthetic_pty_fd_entry_impl(fd, flags, mode, tty_path, pty_index, false);
            return fd;
        }

        if (pty_open_slave_by_path_impl(resolved_path, &pty_index) == 0) {
            int fd = alloc_fd_impl();
            if (fd < 0) {
                pty_close_end_impl(pty_index, false);
                return -1;
            }
            init_synthetic_pty_fd_entry_impl(fd, flags, mode, resolved_path, pty_index, false);
            return fd;
        }
        if (errno != ENOENT) {
            return -1;
        }
    }

    if (vfs_path_is_synthetic(resolved_path)) {
        errno = ENOTSUP;
        return -1;
    }

    ret = vfs_translate_path(resolved_path, translated_path, sizeof(translated_path));
    if (ret != 0) {
        errno = -ret;
        return -1;
    }

    struct linux_stat st;
    bool record_created = vfs_fstatat(AT_FDCWD, resolved_path, &st, 0) == -ENOENT;

    ret = vfs_check_open_permission(resolved_path, translated_path, flags);
    if (ret != 0) {
        errno = -ret;
        return -1;
    }

    int fd = alloc_fd_impl();
    if (fd < 0) {
        return -1;
    }

    int real_fd;
    ret = vfs_open(translated_path, flags, mode, &real_fd);
    if (ret < 0) {
        free_fd_impl(fd);
        errno = -ret;
        return -1;
    }

    init_fd_entry_impl(fd, real_fd, flags, mode, resolved_path);
    if (record_created) {
        vfs_record_created_path(resolved_path, mode);
    }
    return fd;
}

int openat_impl(int dirfd, const char *pathname, int flags, mode_t mode) {
    char translated_path[MAX_PATH];
    char resolved_path[MAX_PATH];
    int ret;

    if (!pathname) {
        errno = EFAULT;
        return -1;
    }

    ret = vfs_resolve_virtual_path_at_follow(dirfd, pathname, resolved_path, sizeof(resolved_path),
                                             (flags & O_NOFOLLOW) == 0);
    if (ret != 0) {
        errno = -ret;
        return -1;
    }

    if ((strcmp(resolved_path, "/proc") == 0 || strcmp(resolved_path, "/sys") == 0) &&
        ((flags & O_DIRECTORY) != 0 || (flags & O_PATH) == 0)) {
        int fd = alloc_fd_impl();
        if (fd < 0) {
            return -1;
        }
        init_synthetic_fd_entry_impl(fd, flags, mode, resolved_path);
        return fd;
    }

    if ((strcmp(resolved_path, "/dev") == 0 || strcmp(resolved_path, "/dev/pts") == 0) &&
        ((flags & O_DIRECTORY) != 0 || (flags & O_PATH) == 0)) {
        int fd = alloc_fd_impl();
        if (fd < 0) {
            return -1;
        }
        synthetic_dir_class_t dir_class = strcmp(resolved_path, "/dev") == 0 ? SYNTHETIC_DIR_DEV : SYNTHETIC_DIR_DEV_PTS;
        init_synthetic_subdir_fd_entry_impl(fd, flags, mode, resolved_path, dir_class);
        return fd;
    }

    {
        proc_self_path_class_t proc_class = vfs_classify_proc_self_path(resolved_path);
        if ((proc_class == PROC_SELF_DIR || proc_class == PROC_SELF_FD_DIR || proc_class == PROC_SELF_FDINFO_DIR || proc_class == PROC_SELF_NS_DIR) && ((flags & O_DIRECTORY) != 0 || (flags & O_PATH) == 0)) {
            int fd = alloc_fd_impl();
            if (fd < 0) {
                return -1;
            }
            synthetic_dir_class_t dir_class = SYNTHETIC_DIR_PROC_SELF;
            if (proc_class == PROC_SELF_FD_DIR) {
                dir_class = SYNTHETIC_DIR_PROC_SELF_FD;
            } else if (proc_class == PROC_SELF_FDINFO_DIR) {
                dir_class = SYNTHETIC_DIR_PROC_SELF_FDINFO;
            } else if (proc_class == PROC_SELF_NS_DIR) {
                dir_class = SYNTHETIC_DIR_PROC_SELF_NS;
            }
            init_synthetic_subdir_fd_entry_impl(fd, flags, mode, resolved_path, dir_class);
            return fd;
        }
        {
            int proc_fd;
            int proc_ret = try_open_proc_self_file(resolved_path, flags, mode, proc_class, &proc_fd);
            if (proc_ret != 0) {
                return (proc_ret < 0) ? -1 : proc_fd;
            }
        }
    }

    {
        synthetic_dev_node_t dev_node = vfs_path_is_synthetic_dev_node(resolved_path);
        if (dev_node == SYNTHETIC_DEV_PTMX) {
            unsigned int pty_index;
            if (pty_allocate_pair_impl(&pty_index) != 0) {
                return -1;
            }
            int fd = alloc_fd_impl();
            if (fd < 0) {
                pty_close_end_impl(pty_index, true);
                return -1;
            }
            init_synthetic_pty_fd_entry_impl(fd, flags, mode, resolved_path, pty_index, true);
            return fd;
        }
        if (dev_node != SYNTHETIC_DEV_NONE) {
            int fd = alloc_fd_impl();
            if (fd < 0) {
                return -1;
            }
            init_synthetic_dev_fd_entry_impl(fd, flags, mode, resolved_path, dev_node);
            return fd;
        }
    }

    {
        unsigned int pty_index;
        if (strcmp(resolved_path, "/dev/tty") == 0) {
            char tty_path[MAX_PATH];
            if (pty_open_controlling_slave_impl(&pty_index) != 0) {
                return -1;
            }
            if (pty_format_slave_path_impl(pty_index, tty_path, sizeof(tty_path)) != 0) {
                pty_close_end_impl(pty_index, false);
                return -1;
            }
            int fd = alloc_fd_impl();
            if (fd < 0) {
                pty_close_end_impl(pty_index, false);
                return -1;
            }
            init_synthetic_pty_fd_entry_impl(fd, flags, mode, tty_path, pty_index, false);
            return fd;
        }

        if (pty_open_slave_by_path_impl(resolved_path, &pty_index) == 0) {
            int fd = alloc_fd_impl();
            if (fd < 0) {
                pty_close_end_impl(pty_index, false);
                return -1;
            }
            init_synthetic_pty_fd_entry_impl(fd, flags, mode, resolved_path, pty_index, false);
            return fd;
        }
        if (errno != ENOENT) {
            return -1;
        }
    }

    if (vfs_path_is_synthetic(resolved_path)) {
        errno = ENOTSUP;
        return -1;
    }

    ret = vfs_translate_path(resolved_path, translated_path, sizeof(translated_path));
    if (ret != 0) {
        errno = -ret;
        return -1;
    }

    struct linux_stat st;
    bool record_created = vfs_fstatat(AT_FDCWD, resolved_path, &st, 0) == -ENOENT;

    ret = vfs_check_open_permission(resolved_path, translated_path, flags);
    if (ret != 0) {
        errno = -ret;
        return -1;
    }

    int fd = alloc_fd_impl();
    if (fd < 0) {
        return -1;
    }

    int real_fd;
    ret = vfs_open(translated_path, flags, mode, &real_fd);
    if (ret < 0) {
        free_fd_impl(fd);
        errno = -ret;
        return -1;
    }

    init_fd_entry_impl(fd, real_fd, flags, mode, resolved_path);
    if (record_created) {
        vfs_record_created_path(resolved_path, mode);
    }
    return fd;
}

int creat_impl(const char *pathname, mode_t mode) {
    return open_impl(pathname, O_WRONLY | O_CREAT | O_TRUNC, mode);
}

__attribute__((visibility("default"))) int open(const char *pathname, int flags, ...) {
    mode_t mode = 0;
    if (flags & O_CREAT) {
        va_list args;
        va_start(args, flags);
        mode = va_arg(args, int);
        va_end(args);
    }
    return open_impl(pathname, flags, mode);
}

__attribute__((visibility("default"))) int openat(int dirfd, const char *pathname, int flags, ...) {
    mode_t mode = 0;
    if (flags & O_CREAT) {
        va_list args;
        va_start(args, flags);
        mode = va_arg(args, int);
        va_end(args);
    }
    return openat_impl(dirfd, pathname, flags, mode);
}

__attribute__((visibility("default"))) int creat(const char *pathname, mode_t mode) {
    return creat_impl(pathname, mode);
}

__attribute__((visibility("default"))) int close(int fd) {
    return close_impl(fd);
}

#include <linux/fcntl.h>

#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "fdtable.h"
#include "internal/ios/fs/sync.h"
#include "pty.h"
#include "vfs.h"

#define MAX_PATH 4096

int open_impl(const char *pathname, int flags, mode_t mode) {
    char translated_path[MAX_PATH];
    char resolved_path[MAX_PATH];
    int ret;

    if (!pathname) {
        errno = EFAULT;
        return -1;
    }

    ret = vfs_resolve_virtual_path_task(pathname, resolved_path, sizeof(resolved_path), NULL);
    if (ret != 0) {
        errno = -ret;
        return -1;
    }

    /* Always create synthetic fds for /proc, /sys, /dev */
    if ((strcmp(resolved_path, "/proc") == 0 || 
         strcmp(resolved_path, "/sys") == 0 ||
         strcmp(resolved_path, "/dev") == 0) && ((flags & O_DIRECTORY) != 0 || (flags & O_PATH) == 0)) {
        int fd = alloc_fd_impl();
        if (fd < 0) {
            return -1;
        }
        init_synthetic_fd_entry_impl(fd, flags, mode, resolved_path);
        return fd;
    }

    {
        proc_self_path_class_t proc_class = vfs_classify_proc_self_path(resolved_path);
        if ((proc_class == PROC_SELF_DIR || proc_class == PROC_SELF_FD_DIR || proc_class == PROC_SELF_FDINFO_DIR) && ((flags & O_DIRECTORY) != 0 || (flags & O_PATH) == 0)) {
            int fd = alloc_fd_impl();
            if (fd < 0) {
                return -1;
            }
            synthetic_dir_class_t dir_class = SYNTHETIC_DIR_PROC_SELF;
            if (proc_class == PROC_SELF_FD_DIR) {
                dir_class = SYNTHETIC_DIR_PROC_SELF_FD;
            } else if (proc_class == PROC_SELF_FDINFO_DIR) {
                dir_class = SYNTHETIC_DIR_PROC_SELF_FDINFO;
            }
            init_synthetic_subdir_fd_entry_impl(fd, flags, mode, resolved_path, dir_class);
            return fd;
        }
    if (proc_class == PROC_SELF_CMDLINE_FILE) {
        int fd = alloc_fd_impl();
        if (fd < 0) {
            return -1;
        }
        init_synthetic_proc_file_fd_entry_impl(fd, flags, mode, resolved_path, SYNTHETIC_PROC_FILE_CMDLINE);
        return fd;
    }
    if (proc_class == PROC_SELF_COMM_FILE) {
        int fd = alloc_fd_impl();
        if (fd < 0) {
            return -1;
        }
        init_synthetic_proc_file_fd_entry_impl(fd, flags, mode, resolved_path, SYNTHETIC_PROC_FILE_COMM);
        return fd;
    }
    if (proc_class == PROC_SELF_STAT_FILE) {
        int fd = alloc_fd_impl();
        if (fd < 0) {
            return -1;
        }
        init_synthetic_proc_file_fd_entry_impl(fd, flags, mode, resolved_path, SYNTHETIC_PROC_FILE_STAT);
        return fd;
    }
        if (proc_class == PROC_SELF_STATM_FILE) {
            int fd = alloc_fd_impl();
            if (fd < 0) {
                return -1;
            }
            init_synthetic_proc_file_fd_entry_impl(fd, flags, mode, resolved_path, SYNTHETIC_PROC_FILE_STATM);
            return fd;
        }
        if (proc_class == PROC_SELF_STATUS_FILE) {
int fd = alloc_fd_impl();
if (fd < 0) {
return -1;
}
init_synthetic_proc_file_fd_entry_impl(fd, flags, mode, resolved_path, SYNTHETIC_PROC_FILE_STATUS);
return fd;
}
        if (proc_class == PROC_SELF_FDINFO_FILE) {
            /* Proc files are read-only; reject write-only or read-write open attempts */
            if ((flags & O_ACCMODE) == O_WRONLY || (flags & O_ACCMODE) == O_RDWR) {
                errno = EACCES;
                return -1;
            }
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
            int fd = alloc_fd_impl();
            if (fd < 0) {
                return -1;
            }
            init_synthetic_proc_file_fd_entry_with_fdnum_impl(fd, flags, mode, resolved_path, SYNTHETIC_PROC_FILE_FDINFO, (int)fd_num);
            return fd;
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

    ret = vfs_resolve_virtual_path_at(dirfd, pathname, resolved_path, sizeof(resolved_path));
    if (ret != 0) {
        errno = -ret;
        return -1;
    }

    if (vfs_path_is_synthetic_root(resolved_path) && ((flags & O_DIRECTORY) != 0 || (flags & O_PATH) == 0)) {
        int fd = alloc_fd_impl();
        if (fd < 0) {
            return -1;
        }
        init_synthetic_fd_entry_impl(fd, flags, mode, resolved_path);
        return fd;
    }

    {
        proc_self_path_class_t proc_class = vfs_classify_proc_self_path(resolved_path);
        if ((proc_class == PROC_SELF_DIR || proc_class == PROC_SELF_FD_DIR || proc_class == PROC_SELF_FDINFO_DIR) && ((flags & O_DIRECTORY) != 0 || (flags & O_PATH) == 0)) {
            int fd = alloc_fd_impl();
            if (fd < 0) {
                return -1;
            }
            synthetic_dir_class_t dir_class = SYNTHETIC_DIR_PROC_SELF;
            if (proc_class == PROC_SELF_FD_DIR) {
                dir_class = SYNTHETIC_DIR_PROC_SELF_FD;
            } else if (proc_class == PROC_SELF_FDINFO_DIR) {
                dir_class = SYNTHETIC_DIR_PROC_SELF_FDINFO;
            }
            init_synthetic_subdir_fd_entry_impl(fd, flags, mode, resolved_path, dir_class);
            return fd;
        }
    if (proc_class == PROC_SELF_CMDLINE_FILE) {
        int fd = alloc_fd_impl();
        if (fd < 0) {
            return -1;
        }
        init_synthetic_proc_file_fd_entry_impl(fd, flags, mode, resolved_path, SYNTHETIC_PROC_FILE_CMDLINE);
        return fd;
    }
    if (proc_class == PROC_SELF_COMM_FILE) {
        int fd = alloc_fd_impl();
        if (fd < 0) {
            return -1;
        }
        init_synthetic_proc_file_fd_entry_impl(fd, flags, mode, resolved_path, SYNTHETIC_PROC_FILE_COMM);
        return fd;
    }
    if (proc_class == PROC_SELF_STAT_FILE) {
        int fd = alloc_fd_impl();
        if (fd < 0) {
            return -1;
        }
        init_synthetic_proc_file_fd_entry_impl(fd, flags, mode, resolved_path, SYNTHETIC_PROC_FILE_STAT);
        return fd;
    }
        if (proc_class == PROC_SELF_STATM_FILE) {
            int fd = alloc_fd_impl();
            if (fd < 0) {
                return -1;
            }
            init_synthetic_proc_file_fd_entry_impl(fd, flags, mode, resolved_path, SYNTHETIC_PROC_FILE_STATM);
            return fd;
        }
        if (proc_class == PROC_SELF_FDINFO_FILE) {
            /* Proc files are read-only; reject write-only or read-write open attempts */
            if ((flags & O_ACCMODE) == O_WRONLY || (flags & O_ACCMODE) == O_RDWR) {
                errno = EACCES;
                return -1;
            }
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
            int fd = alloc_fd_impl();
            if (fd < 0) {
                return -1;
            }
            init_synthetic_proc_file_fd_entry_with_fdnum_impl(fd, flags, mode, resolved_path, SYNTHETIC_PROC_FILE_FDINFO, (int)fd_num);
            return fd;
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

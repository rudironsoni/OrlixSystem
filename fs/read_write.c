/* IXLandSystem/fs/read_write.c
 * Virtual read/write/lseek implementation
 */

#include <linux/fcntl.h>
#include <linux/fs.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fdtable.h"
#include "internal/ios/fs/file_io_host.h"
#include "internal/ios/fs/sync.h"
#include "pipe.h"
#include "pty.h"
#include "vfs.h"

/* Linux-owner type for file offsets (matches __kernel_off_t) */
typedef long long linux_off_t;

ssize_t read_impl(int fd, void *buf, size_t count) {
    if (count == 0) {
        return 0;
    }
    if (!buf) {
        errno = EFAULT;
        return -1;
    }

    if (fd < 0 || fd >= NR_OPEN_DEFAULT) {
        errno = EBADF;
        return -1;
    }

    void *entry = get_fd_entry_impl(fd);
    if (!entry) {
        errno = EBADF;
        return -1;
    }

    /* Directory check comes before access mode (EISDIR is more specific) */
    if (get_fd_is_synthetic_dir_impl(entry)) {
        put_fd_entry_impl(entry);
        errno = EISDIR;
        return -1;
    }

    /* Enforce read access mode before any dispatch */
    if (!get_fd_is_readable_impl(entry)) {
        put_fd_entry_impl(entry);
        errno = EBADF;
        return -1;
    }

    if (get_fd_is_pipe_impl(entry)) {
        struct pipe_endpoint *endpoint = get_fd_pipe_endpoint_impl(entry);
        bool nonblock = (get_fd_flags_impl(entry) & O_NONBLOCK) != 0;
        ssize_t ret = pipe_read_endpoint_impl(endpoint, buf, count, nonblock);
        put_fd_entry_impl(entry);
        return ret;
    }

    if (get_fd_is_synthetic_dev_impl(entry)) {
        synthetic_dev_node_t dev_node = get_fd_synthetic_dev_node_impl(entry);
        put_fd_entry_impl(entry);

        if (dev_node == SYNTHETIC_DEV_NULL) {
            return 0;
        } else if (dev_node == SYNTHETIC_DEV_ZERO) {
            memset(buf, 0, count);
            return (ssize_t)count;
        } else if (dev_node == SYNTHETIC_DEV_RANDOM ||
                   dev_node == SYNTHETIC_DEV_URANDOM) {
            arc4random_buf(buf, count);
            return (ssize_t)count;
        }
        errno = EINVAL;
        return -1;
    }

    if (get_fd_is_synthetic_pty_impl(entry)) {
        unsigned int pty_index = get_fd_synthetic_pty_index_impl(entry);
        bool is_master = get_fd_is_synthetic_pty_master_impl(entry);
        bool nonblock = (get_fd_flags_impl(entry) & O_NONBLOCK) != 0;
        put_fd_entry_impl(entry);

        if (is_master) {
            return pty_read_master_impl(pty_index, buf, count, nonblock);
        }
        return pty_read_slave_impl(pty_index, buf, count, nonblock);
    }

    if (get_fd_is_synthetic_proc_file_impl(entry)) {
        synthetic_proc_file_t proc_file = get_fd_synthetic_proc_file_impl(entry);
        int fd_num = get_fd_proc_file_fd_num_impl(entry);
        int target_pid = get_fd_proc_file_target_pid_impl(entry);
        char content[8192];
        int content_len = -EINVAL;

        switch (proc_file) {
        case SYNTHETIC_PROC_FILE_CMDLINE:
            content_len = vfs_proc_task_cmdline_content(target_pid, content, sizeof(content));
            break;
        case SYNTHETIC_PROC_FILE_ENVIRON:
            content_len = vfs_proc_task_environ_content(target_pid, content, sizeof(content));
            break;
        case SYNTHETIC_PROC_FILE_COMM:
            content_len = vfs_proc_task_comm_content(target_pid, content, sizeof(content));
            break;
        case SYNTHETIC_PROC_FILE_STAT:
            content_len = vfs_proc_task_stat_content(target_pid, content, sizeof(content));
            break;
        case SYNTHETIC_PROC_FILE_STATM:
            content_len = vfs_proc_task_statm_content(target_pid, content, sizeof(content));
            break;
        case SYNTHETIC_PROC_FILE_MAPS:
            content_len = vfs_proc_task_maps_content(target_pid, content, sizeof(content));
            break;
        case SYNTHETIC_PROC_FILE_SMAPS:
            content_len = vfs_proc_task_smaps_content(target_pid, content, sizeof(content));
            break;
        case SYNTHETIC_PROC_FILE_STATUS:
            content_len = vfs_proc_task_status_content(target_pid, content, sizeof(content));
            break;
        case SYNTHETIC_PROC_FILE_MOUNTINFO:
            content_len = vfs_proc_task_mountinfo_content(target_pid, content, sizeof(content));
            break;
        case SYNTHETIC_PROC_FILE_MOUNTS:
            content_len = vfs_proc_task_mounts_content(target_pid, content, sizeof(content));
            break;
        case SYNTHETIC_PROC_FILE_FDINFO:
            content_len = vfs_proc_task_fdinfo_content(target_pid, fd_num, content, sizeof(content));
            break;
        case SYNTHETIC_PROC_FILE_FILESYSTEMS:
            content_len = vfs_proc_filesystems_content(content, sizeof(content));
            break;
        case SYNTHETIC_PROC_FILE_MEMINFO:
            content_len = vfs_proc_meminfo_content(content, sizeof(content));
            break;
        case SYNTHETIC_PROC_FILE_CPUINFO:
            content_len = vfs_proc_cpuinfo_content(content, sizeof(content));
            break;
        default:
            content_len = -EINVAL;
            break;
        }

        if (content_len < 0) {
            put_fd_entry_impl(entry);
            errno = -content_len;
            return -1;
        }

        linux_off_t offset = (linux_off_t)get_fd_offset_impl(entry);
        if (offset < 0) {
            offset = 0;
        }
        if ((size_t)offset >= (size_t)content_len) {
            put_fd_entry_impl(entry);
            return 0;
        }

        size_t available = (size_t)content_len - (size_t)offset;
        size_t to_copy = (count < available) ? count : available;
        memcpy(buf, content + offset, to_copy);
        set_fd_offset_impl((fd_entry_t *)entry, offset + (linux_off_t)to_copy);
        put_fd_entry_impl(entry);
        return (ssize_t)to_copy;
    }

    int real_fd = get_real_fd_impl(entry);
    ssize_t bytes = host_read_impl(real_fd, buf, count);
    if (bytes > 0) {
        linux_off_t pos = (linux_off_t)host_lseek_impl(real_fd, 0, SEEK_CUR);
        if (pos >= 0) {
            set_fd_offset_impl((fd_entry_t *)entry, pos);
        }
    }
    put_fd_entry_impl(entry);
    return bytes;
}

ssize_t write_impl(int fd, const void *buf, size_t count) {
    if (count == 0) {
        return 0;
    }
    if (!buf) {
        errno = EFAULT;
        return -1;
    }

    if (fd < 0 || fd >= NR_OPEN_DEFAULT) {
        errno = EBADF;
        return -1;
    }

    void *entry = get_fd_entry_impl(fd);
    if (!entry) {
        errno = EBADF;
        return -1;
    }

    /* Directory check comes before access mode (EISDIR is more specific) */
    if (get_fd_is_synthetic_dir_impl(entry)) {
        put_fd_entry_impl(entry);
        errno = EISDIR;
        return -1;
    }

    /* Enforce write access mode before any dispatch */
    if (!get_fd_is_writable_impl(entry)) {
        put_fd_entry_impl(entry);
        errno = EBADF;
        return -1;
    }

    if (get_fd_is_pipe_impl(entry)) {
        struct pipe_endpoint *endpoint = get_fd_pipe_endpoint_impl(entry);
        bool nonblock = (get_fd_flags_impl(entry) & O_NONBLOCK) != 0;
        ssize_t ret = pipe_write_endpoint_impl(endpoint, buf, count, nonblock);
        put_fd_entry_impl(entry);
        return ret;
    }

    if (get_fd_is_synthetic_dev_impl(entry)) {
        synthetic_dev_node_t dev_node = get_fd_synthetic_dev_node_impl(entry);
        put_fd_entry_impl(entry);

        if (dev_node == SYNTHETIC_DEV_NULL ||
            dev_node == SYNTHETIC_DEV_ZERO ||
            dev_node == SYNTHETIC_DEV_RANDOM ||
            dev_node == SYNTHETIC_DEV_URANDOM) {
            return (ssize_t)count;
        }
    }

    if (get_fd_is_synthetic_pty_impl(entry)) {
        unsigned int pty_index = get_fd_synthetic_pty_index_impl(entry);
        bool is_master = get_fd_is_synthetic_pty_master_impl(entry);
        bool nonblock = (get_fd_flags_impl(entry) & O_NONBLOCK) != 0;
        put_fd_entry_impl(entry);

        if (is_master) {
            return pty_write_master_impl(pty_index, buf, count, nonblock);
        }

        return pty_write_slave_impl(pty_index, buf, count, nonblock);
    }

    if (get_fd_is_synthetic_proc_file_impl(entry)) {
        put_fd_entry_impl(entry);
        errno = EINVAL;
        return -1;
    }

    int real_fd = get_real_fd_impl(entry);
    linux_off_t current_size = host_lseek_impl(real_fd, 0, SEEK_END);
    if (current_size < 0) {
        put_fd_entry_impl(entry);
        return -1;
    }

    if (get_fd_is_append_impl(entry)) {
        if (host_lseek_impl(real_fd, 0, SEEK_END) < 0) {
            put_fd_entry_impl(entry);
            return -1;
        }
    }

    ssize_t bytes = host_write_impl(real_fd, buf, count);
    if (bytes > 0) {
        linux_off_t pos = (linux_off_t)host_lseek_impl(real_fd, 0, SEEK_CUR);
        if (pos >= 0) {
            set_fd_offset_impl((fd_entry_t *)entry, pos);
        }
    }
    put_fd_entry_impl(entry);
    return bytes;
}

linux_off_t lseek_impl(int fd, linux_off_t offset, int whence) {
    void *entry;

    if (fd < 0 || fd >= NR_OPEN_DEFAULT) {
        errno = EBADF;
        return (linux_off_t)-1;
    }

    entry = get_fd_entry_impl(fd);
    if (!entry) {
        errno = EBADF;
        return (linux_off_t)-1;
    }

    if (get_fd_is_synthetic_dir_impl(entry) || get_fd_is_synthetic_proc_file_impl(entry)) {
        put_fd_entry_impl(entry);
        errno = ESPIPE;
        return (linux_off_t)-1;
    }

    if (get_fd_is_synthetic_dev_impl(entry) || get_fd_is_synthetic_pty_impl(entry)) {
        put_fd_entry_impl(entry);
        errno = ESPIPE;
        return (linux_off_t)-1;
    }

    if (get_fd_is_pipe_impl(entry)) {
        put_fd_entry_impl(entry);
        errno = ESPIPE;
        return (linux_off_t)-1;
    }

    linux_off_t result = (linux_off_t)host_lseek_impl(get_real_fd_impl(entry), (long long)offset, whence);
    if (result >= 0) {
        set_fd_offset_impl((fd_entry_t *)entry, result);
    }
    put_fd_entry_impl(entry);
    return result;
}

/* Generate synthetic proc file content into buf; returns content length or negative errno */
static int synthetic_proc_file_content(synthetic_proc_file_t proc_file, int fd_num, int target_pid,
                                        char *content, size_t content_size) {
    switch (proc_file) {
    case SYNTHETIC_PROC_FILE_CMDLINE:
        return vfs_proc_task_cmdline_content(target_pid, content, content_size);
    case SYNTHETIC_PROC_FILE_ENVIRON:
        return vfs_proc_task_environ_content(target_pid, content, content_size);
    case SYNTHETIC_PROC_FILE_COMM:
        return vfs_proc_task_comm_content(target_pid, content, content_size);
    case SYNTHETIC_PROC_FILE_STAT:
        return vfs_proc_task_stat_content(target_pid, content, content_size);
    case SYNTHETIC_PROC_FILE_STATM:
        return vfs_proc_task_statm_content(target_pid, content, content_size);
    case SYNTHETIC_PROC_FILE_MAPS:
        return vfs_proc_task_maps_content(target_pid, content, content_size);
    case SYNTHETIC_PROC_FILE_SMAPS:
        return vfs_proc_task_smaps_content(target_pid, content, content_size);
    case SYNTHETIC_PROC_FILE_STATUS:
        return vfs_proc_task_status_content(target_pid, content, content_size);
    case SYNTHETIC_PROC_FILE_MOUNTINFO:
        return vfs_proc_task_mountinfo_content(target_pid, content, content_size);
    case SYNTHETIC_PROC_FILE_MOUNTS:
        return vfs_proc_task_mounts_content(target_pid, content, content_size);
    case SYNTHETIC_PROC_FILE_FDINFO:
        return vfs_proc_task_fdinfo_content(target_pid, fd_num, content, content_size);
    case SYNTHETIC_PROC_FILE_FILESYSTEMS:
        return vfs_proc_filesystems_content(content, content_size);
    case SYNTHETIC_PROC_FILE_MEMINFO:
        return vfs_proc_meminfo_content(content, content_size);
    case SYNTHETIC_PROC_FILE_CPUINFO:
        return vfs_proc_cpuinfo_content(content, content_size);
    default:
        return -EINVAL;
    }
}

ssize_t pread_impl(int fd, void *buf, size_t count, linux_off_t offset) {
    if (count == 0) {
        return 0;
    }
    if (!buf) {
        errno = EFAULT;
        return -1;
    }

    if (fd < 0 || fd >= NR_OPEN_DEFAULT) {
        errno = EBADF;
        return -1;
    }

    void *entry = get_fd_entry_impl(fd);
    if (!entry) {
        errno = EBADF;
        return -1;
    }

    /* Directory check comes before access mode (EISDIR is more specific) */
    if (get_fd_is_synthetic_dir_impl(entry)) {
        put_fd_entry_impl(entry);
        errno = EISDIR;
        return -1;
    }

    /* Enforce read access mode before any dispatch */
    if (!get_fd_is_readable_impl(entry)) {
        put_fd_entry_impl(entry);
        errno = EBADF;
        return -1;
    }

    if (get_fd_is_pipe_impl(entry)) {
        put_fd_entry_impl(entry);
        errno = ESPIPE;
        return -1;
    }

    /* Handle synthetic proc files: pread reads from supplied offset, does not change fd offset */
    if (get_fd_is_synthetic_proc_file_impl(entry)) {
        synthetic_proc_file_t proc_file = get_fd_synthetic_proc_file_impl(entry);
        int fd_num = get_fd_proc_file_fd_num_impl(entry);
        int target_pid = get_fd_proc_file_target_pid_impl(entry);
        linux_off_t saved_offset = get_fd_offset_impl(entry);
        char content[8192];
        int content_len = synthetic_proc_file_content(proc_file, fd_num, target_pid, content, sizeof(content));

        if (content_len < 0) {
            set_fd_offset_impl((fd_entry_t *)entry, saved_offset);
            put_fd_entry_impl(entry);
            errno = -content_len;
            return -1;
        }

        if (offset < 0) {
            set_fd_offset_impl((fd_entry_t *)entry, saved_offset);
            put_fd_entry_impl(entry);
            errno = EINVAL;
            return -1;
        }

        if ((linux_off_t)offset >= content_len) {
            set_fd_offset_impl((fd_entry_t *)entry, saved_offset);
            put_fd_entry_impl(entry);
            return 0;
        }

        size_t available = (size_t)content_len - (size_t)offset;
        size_t to_copy = (count < available) ? count : available;
        memcpy(buf, content + offset, to_copy);
        set_fd_offset_impl((fd_entry_t *)entry, saved_offset);
        put_fd_entry_impl(entry);
        return (ssize_t)to_copy;
    }

    ssize_t bytes = host_pread_impl(get_real_fd_impl(entry), buf, count, offset);
    put_fd_entry_impl(entry);
    return bytes;
}

ssize_t pwrite_impl(int fd, const void *buf, size_t count, linux_off_t offset) {
    if (count == 0) {
        return 0;
    }
    if (!buf) {
        errno = EFAULT;
        return -1;
    }

    if (fd < 0 || fd >= NR_OPEN_DEFAULT) {
        errno = EBADF;
        return -1;
    }

    void *entry = get_fd_entry_impl(fd);
    if (!entry) {
        errno = EBADF;
        return -1;
    }

    /* Directory check comes before access mode (EISDIR is more specific) */
    if (get_fd_is_synthetic_dir_impl(entry)) {
        put_fd_entry_impl(entry);
        errno = EISDIR;
        return -1;
    }

    /* Enforce write access mode before any dispatch */
    if (!get_fd_is_writable_impl(entry)) {
        put_fd_entry_impl(entry);
        errno = EBADF;
        return -1;
    }

    if (get_fd_is_pipe_impl(entry)) {
        put_fd_entry_impl(entry);
        errno = ESPIPE;
        return -1;
    }

    /* Synthetic proc files are read-only */
    if (get_fd_is_synthetic_proc_file_impl(entry)) {
        put_fd_entry_impl(entry);
        errno = EINVAL;
        return -1;
    }

    /* Synthetic devices accept writes without host fallback */
    if (get_fd_is_synthetic_dev_impl(entry)) {
        put_fd_entry_impl(entry);
        return (ssize_t)count;
    }

    /* PTY writes handled separately */
    if (get_fd_is_synthetic_pty_impl(entry)) {
        put_fd_entry_impl(entry);
        errno = EINVAL;
        return -1;
    }

    ssize_t bytes = host_pwrite_impl(get_real_fd_impl(entry), buf, count, offset);
    put_fd_entry_impl(entry);
    return bytes;
}

__attribute__((visibility("default"))) ssize_t read(int fd, void *buf, size_t count) {
    return read_impl(fd, buf, count);
}

__attribute__((visibility("default"))) ssize_t write(int fd, const void *buf, size_t count) {
    return write_impl(fd, buf, count);
}

__attribute__((visibility("default"))) long long lseek(int fd, long long offset, int whence) {
    return lseek_impl(fd, offset, whence);
}

__attribute__((visibility("default"))) ssize_t pread(int fd, void *buf, size_t count, long long offset) {
    return pread_impl(fd, buf, count, offset);
}

__attribute__((visibility("default"))) ssize_t pwrite(int fd, const void *buf, size_t count, long long offset) {
    return pwrite_impl(fd, buf, count, offset);
}

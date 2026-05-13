/* OrlixKernel/fs/read_write.c
 * Virtual read/write/lseek implementation
 */

#include <uapi/linux/fcntl.h>
#include <uapi/linux/fs.h>
#include <uapi/asm/stat.h>
#include <uapi/linux/uio.h>

#include <linux/errno.h>
#include <linux/string.h>

#include "fdtable.h"
#include "private/fs/fdtable_state.h"
#include "internal/fs/file.h"
#include "internal/random.h"
#include "pipe.h"
#include "private/fs/pipe_state.h"
#include "private/fs/pty_state.h"
#include "vfs.h"
#include "private/fs/vfs_state.h"
#include "kernel/cgroup.h"
#include "kernel/net/socket.h"
#include "kernel/task.h"
#include "private/kernel/task_state.h"

extern int ftruncate_impl(int fd, int64_t length);
extern int fdatasync_impl(int fd);

ssize_t read_impl(int fd, void *buf, size_t count) {
    if (count == 0) {
        return 0;
    }
    if (!buf) {
        return -EFAULT;
    }

    if (fd < 0 || fd >= NR_OPEN_DEFAULT) {
        return -EBADF;
    }

    void *entry = get_fd_entry_impl(fd);
    if (!entry) {
        return -EBADF;
    }

    /* Directory check comes before access mode (EISDIR is more specific) */
    if (get_fd_is_synthetic_dir_impl(entry)) {
        put_fd_entry_impl(entry);
        return -EISDIR;
    }

    /* Enforce read access mode before any dispatch */
    if (!get_fd_is_readable_impl(entry)) {
        put_fd_entry_impl(entry);
        return -EBADF;
    }

    if (get_fd_is_pipe_impl(entry)) {
        struct pipe_endpoint *endpoint = get_fd_pipe_endpoint_impl(entry);
        bool nonblock = (get_fd_flags_impl(entry) & O_NONBLOCK) != 0;
        ssize_t ret = pipe_read_endpoint_impl(endpoint, buf, count, nonblock);
        if (ret == -EINTR && !nonblock) {
            task_restart_record_impl(task_current(), TASK_RESTART_PIPE_READ,
                                     (uint64_t)fd, (uint64_t)(uintptr_t)buf,
                                     (uint64_t)count, 0, 0, 0);
        }
        put_fd_entry_impl(entry);
        return ret;
    }

    if (get_fd_is_socket_impl(entry)) {
        struct socket_state *sock = get_fd_socket_impl(entry);
        bool nonblock = (get_fd_flags_impl(entry) & O_NONBLOCK) != 0;
        put_fd_entry_impl(entry);
        return socket_recv_impl(sock, buf, count, nonblock);
    }

    if (get_fd_is_eventfd_impl(entry)) {
        ssize_t ret = (ssize_t)eventfd_read_entry_impl(entry, buf, count);
        put_fd_entry_impl(entry);
        return ret;
    }

    if (get_fd_is_timerfd_impl(entry)) {
        ssize_t ret = (ssize_t)timerfd_read_entry_impl(entry, buf, count);
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
            get_random_bytes(buf, count);
            return (ssize_t)count;
        }
        return -EINVAL;
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
        char content[32768];
        int content_len = -EINVAL;
        char path[MAX_PATH];

        if (get_fd_has_cgroupfs_path_impl(entry) &&
            get_fd_cgroupfs_path_impl(entry, path, sizeof(path)) == 0) {
            content_len = cgroupfs_read_node(path,
                                             (enum cgroupfs_node_type)get_fd_cgroupfs_node_impl(entry),
                                             content, sizeof(content));
            if (content_len < 0) {
                put_fd_entry_impl(entry);
                return content_len;
            }
            int64_t offset = get_fd_offset_impl(entry);
            if (offset < 0) {
                offset = 0;
            }
            if ((size_t)offset >= (size_t)content_len) {
                put_fd_entry_impl(entry);
                return 0;
            }
            size_t available = (size_t)content_len - (size_t)offset;
            size_t to_copy = count < available ? count : available;
            memcpy(buf, content + offset, to_copy);
            set_fd_offset_impl((fd_entry_t *)entry, offset + (int64_t)to_copy);
            put_fd_entry_impl(entry);
            return (ssize_t)to_copy;
        }

        if (get_fd_path_impl(entry, path, sizeof(path)) == 0 &&
            cgroupfs_classify_path(path) != CGROUPFS_NODE_NONE) {
            content_len = cgroupfs_read_path(path, content, sizeof(content));
            if (content_len < 0) {
                put_fd_entry_impl(entry);
                return content_len;
            }
            int64_t offset = get_fd_offset_impl(entry);
            if (offset < 0) {
                offset = 0;
            }
            if ((size_t)offset >= (size_t)content_len) {
                put_fd_entry_impl(entry);
                return 0;
            }
            size_t available = (size_t)content_len - (size_t)offset;
            size_t to_copy = count < available ? count : available;
            memcpy(buf, content + offset, to_copy);
            set_fd_offset_impl((fd_entry_t *)entry, offset + (int64_t)to_copy);
            put_fd_entry_impl(entry);
            return (ssize_t)to_copy;
        }

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
        case SYNTHETIC_PROC_FILE_CGROUP:
            content_len = vfs_proc_task_cgroup_content(target_pid, content, sizeof(content));
            break;
        case SYNTHETIC_PROC_FILE_UID_MAP:
            content_len = vfs_proc_task_uid_map_content(target_pid, content, sizeof(content));
            break;
        case SYNTHETIC_PROC_FILE_GID_MAP:
            content_len = vfs_proc_task_gid_map_content(target_pid, content, sizeof(content));
            break;
        case SYNTHETIC_PROC_FILE_SETGROUPS:
            content_len = vfs_proc_task_setgroups_content(target_pid, content, sizeof(content));
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
            return content_len;
        }

        int64_t offset = get_fd_offset_impl(entry);
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
        set_fd_offset_impl((fd_entry_t *)entry, offset + (int64_t)to_copy);
        put_fd_entry_impl(entry);
        return (ssize_t)to_copy;
    }

    int real_fd = get_real_fd_impl(entry);
    ssize_t bytes = backing_read(real_fd, buf, count);
    if (bytes > 0) {
        int64_t pos = backing_lseek(real_fd, 0, SEEK_CUR);
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
        return -EFAULT;
    }

    if (fd < 0 || fd >= NR_OPEN_DEFAULT) {
        return -EBADF;
    }

    void *entry = get_fd_entry_impl(fd);
    if (!entry) {
        return -EBADF;
    }

    /* Directory check comes before access mode (EISDIR is more specific) */
    if (get_fd_is_synthetic_dir_impl(entry)) {
        put_fd_entry_impl(entry);
        return -EISDIR;
    }

    /* Enforce write access mode before any dispatch */
    if (!get_fd_is_writable_impl(entry)) {
        put_fd_entry_impl(entry);
        return -EBADF;
    }

    if (get_fd_is_pipe_impl(entry)) {
        struct pipe_endpoint *endpoint = get_fd_pipe_endpoint_impl(entry);
        bool nonblock = (get_fd_flags_impl(entry) & O_NONBLOCK) != 0;
        ssize_t ret = pipe_write_endpoint_impl(endpoint, buf, count, nonblock);
        if (ret == -EINTR && !nonblock) {
            task_restart_record_impl(task_current(), TASK_RESTART_PIPE_WRITE,
                                     (uint64_t)fd, (uint64_t)(uintptr_t)buf,
                                     (uint64_t)count, 0, 0, 0);
        }
        put_fd_entry_impl(entry);
        return ret;
    }

    if (get_fd_is_socket_impl(entry)) {
        struct socket_state *sock = get_fd_socket_impl(entry);
        bool nonblock = (get_fd_flags_impl(entry) & O_NONBLOCK) != 0;
        put_fd_entry_impl(entry);
        return socket_send_impl(sock, buf, count, nonblock);
    }

    if (get_fd_is_eventfd_impl(entry)) {
        ssize_t ret = (ssize_t)eventfd_write_entry_impl(entry, buf, count);
        put_fd_entry_impl(entry);
        return ret;
    }

    if (get_fd_is_timerfd_impl(entry)) {
        ssize_t ret = (ssize_t)timerfd_write_entry_impl(entry, buf, count);
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
        char path[MAX_PATH];
        if (get_fd_has_cgroupfs_path_impl(entry) &&
            get_fd_cgroupfs_path_impl(entry, path, sizeof(path)) == 0) {
            long ret = cgroupfs_write_node(path,
                                           (enum cgroupfs_node_type)get_fd_cgroupfs_node_impl(entry),
                                           (const char *)buf, count);
            put_fd_entry_impl(entry);
            if (ret < 0) {
                return ret;
            }
            return ret;
        }

        if (get_fd_path_impl(entry, path, sizeof(path)) == 0 &&
            cgroupfs_classify_path(path) != CGROUPFS_NODE_NONE) {
            long ret = cgroupfs_write_path(path, (const char *)buf, count);
            put_fd_entry_impl(entry);
            if (ret < 0) {
                return ret;
            }
            return (ssize_t)ret;
        }
        {
            synthetic_proc_file_t proc_file = get_fd_synthetic_proc_file_impl(entry);
            if (proc_file == SYNTHETIC_PROC_FILE_UID_MAP ||
                proc_file == SYNTHETIC_PROC_FILE_GID_MAP ||
                proc_file == SYNTHETIC_PROC_FILE_SETGROUPS) {
                int target_pid = get_fd_proc_file_target_pid_impl(entry);
                long ret = vfs_proc_task_write_id_map_content(proc_file, target_pid,
                                                              (const char *)buf, count);
                put_fd_entry_impl(entry);
                if (ret < 0) {
                    return ret;
                }
                return ret;
            }
        }
        put_fd_entry_impl(entry);
        return -EINVAL;
    }

    if (get_fd_is_memfd_impl(entry) &&
        memfd_write_allowed_entry_impl(entry, get_fd_offset_impl((fd_entry_t *)entry), count) != 0) {
        put_fd_entry_impl(entry);
        return -1;
    }

    int real_fd = get_real_fd_impl(entry);
    if (get_fd_is_memfd_impl(entry)) {
        struct stat st;
        int64_t offset = get_fd_offset_impl((fd_entry_t *)entry);
        int64_t end = offset + (int64_t)count;

        if (end < offset) {
            put_fd_entry_impl(entry);
            return -EFBIG;
        }
        if (backing_fstat(real_fd, &st) != 0) {
            put_fd_entry_impl(entry);
            return -EIO;
        }
        if (end > st.st_size && backing_ftruncate(real_fd, end) != 0) {
            put_fd_entry_impl(entry);
            return -1;
        }
    }

    if (get_fd_is_append_impl(entry)) {
        if (backing_lseek(real_fd, 0, SEEK_END) < 0) {
            put_fd_entry_impl(entry);
            return -1;
        }
    }

    ssize_t bytes = backing_write(real_fd, buf, count);
    if (bytes > 0) {
        int64_t pos = backing_lseek(real_fd, 0, SEEK_CUR);
        if (pos >= 0) {
            set_fd_offset_impl((fd_entry_t *)entry, pos);
        }
    }
    put_fd_entry_impl(entry);
    return bytes;
}

int64_t lseek_impl(int fd, int64_t offset, int whence) {
    void *entry;

    if (fd < 0 || fd >= NR_OPEN_DEFAULT) {
        return -EBADF;
    }

    entry = get_fd_entry_impl(fd);
    if (!entry) {
        return -EBADF;
    }

    if (get_fd_is_synthetic_dir_impl(entry) || get_fd_is_synthetic_proc_file_impl(entry)) {
        put_fd_entry_impl(entry);
        return -ESPIPE;
    }

    if (get_fd_is_synthetic_dev_impl(entry) || get_fd_is_synthetic_pty_impl(entry)) {
        put_fd_entry_impl(entry);
        return -ESPIPE;
    }

    if (get_fd_is_pipe_impl(entry)) {
        put_fd_entry_impl(entry);
        return -ESPIPE;
    }

    int64_t result = backing_lseek(get_real_fd_impl(entry), offset, whence);
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
    case SYNTHETIC_PROC_FILE_CGROUP:
        return vfs_proc_task_cgroup_content(target_pid, content, content_size);
    case SYNTHETIC_PROC_FILE_UID_MAP:
        return vfs_proc_task_uid_map_content(target_pid, content, content_size);
    case SYNTHETIC_PROC_FILE_GID_MAP:
        return vfs_proc_task_gid_map_content(target_pid, content, content_size);
    case SYNTHETIC_PROC_FILE_SETGROUPS:
        return vfs_proc_task_setgroups_content(target_pid, content, content_size);
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

ssize_t pread_impl(int fd, void *buf, size_t count, int64_t offset) {
    if (count == 0) {
        return 0;
    }
    if (!buf) {
        return -EFAULT;
    }

    if (fd < 0 || fd >= NR_OPEN_DEFAULT) {
        return -EBADF;
    }

    void *entry = get_fd_entry_impl(fd);
    if (!entry) {
        return -EBADF;
    }

    /* Directory check comes before access mode (EISDIR is more specific) */
    if (get_fd_is_synthetic_dir_impl(entry)) {
        put_fd_entry_impl(entry);
        return -EISDIR;
    }

    /* Enforce read access mode before any dispatch */
    if (!get_fd_is_readable_impl(entry)) {
        put_fd_entry_impl(entry);
        return -EBADF;
    }

    if (get_fd_is_pipe_impl(entry)) {
        put_fd_entry_impl(entry);
        return -ESPIPE;
    }

    /* Handle synthetic proc files: pread reads from supplied offset, does not change fd offset */
    if (get_fd_is_synthetic_proc_file_impl(entry)) {
        synthetic_proc_file_t proc_file = get_fd_synthetic_proc_file_impl(entry);
        int fd_num = get_fd_proc_file_fd_num_impl(entry);
        int target_pid = get_fd_proc_file_target_pid_impl(entry);
        int64_t saved_offset = get_fd_offset_impl(entry);
        char content[32768];
        int content_len = synthetic_proc_file_content(proc_file, fd_num, target_pid, content, sizeof(content));

        if (content_len < 0) {
            set_fd_offset_impl((fd_entry_t *)entry, saved_offset);
            put_fd_entry_impl(entry);
            return content_len;
        }

        if (offset < 0) {
            set_fd_offset_impl((fd_entry_t *)entry, saved_offset);
            put_fd_entry_impl(entry);
            return -EINVAL;
        }

        if ((int64_t)offset >= content_len) {
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

    ssize_t bytes = backing_pread(get_real_fd_impl(entry), buf, count, offset);
    put_fd_entry_impl(entry);
    return bytes;
}

ssize_t pwrite_impl(int fd, const void *buf, size_t count, int64_t offset) {
    if (count == 0) {
        return 0;
    }
    if (!buf) {
        return -EFAULT;
    }

    if (fd < 0 || fd >= NR_OPEN_DEFAULT) {
        return -EBADF;
    }

    void *entry = get_fd_entry_impl(fd);
    if (!entry) {
        return -EBADF;
    }

    /* Directory check comes before access mode (EISDIR is more specific) */
    if (get_fd_is_synthetic_dir_impl(entry)) {
        put_fd_entry_impl(entry);
        return -EISDIR;
    }

    /* Enforce write access mode before any dispatch */
    if (!get_fd_is_writable_impl(entry)) {
        put_fd_entry_impl(entry);
        return -EBADF;
    }

    if (get_fd_is_pipe_impl(entry)) {
        put_fd_entry_impl(entry);
        return -ESPIPE;
    }

    /* Synthetic proc files are read-only */
    if (get_fd_is_synthetic_proc_file_impl(entry)) {
        put_fd_entry_impl(entry);
        return -EINVAL;
    }

    /* Synthetic devices accept writes without host fallback */
    if (get_fd_is_synthetic_dev_impl(entry)) {
        put_fd_entry_impl(entry);
        return (ssize_t)count;
    }

    /* PTY writes handled separately */
    if (get_fd_is_synthetic_pty_impl(entry)) {
        put_fd_entry_impl(entry);
        return -EINVAL;
    }

    if (get_fd_is_memfd_impl(entry) && memfd_write_allowed_entry_impl(entry, offset, count) != 0) {
        put_fd_entry_impl(entry);
        return -1;
    }

    ssize_t bytes = backing_pwrite(get_real_fd_impl(entry), buf, count, offset);
    put_fd_entry_impl(entry);
    return bytes;
}

ssize_t copy_file_range_impl(int fd_in, int64_t *off_in, int fd_out,
                             int64_t *off_out, size_t len, unsigned int flags) {
    char buffer[16384];
    size_t copied = 0;

    if (flags != 0) {
        return -EINVAL;
    }
    if (len == 0) {
        return 0;
    }

    while (copied < len) {
        size_t chunk = len - copied;
        ssize_t nread;
        ssize_t nwritten;
        size_t written = 0;

        if (chunk > sizeof(buffer)) {
            chunk = sizeof(buffer);
        }

        if (off_in) {
            nread = pread_impl(fd_in, buffer, chunk, *off_in);
        } else {
            nread = read_impl(fd_in, buffer, chunk);
        }
        if (nread < 0) {
            return copied > 0 ? (ssize_t)copied : nread;
        }
        if (nread == 0) {
            break;
        }

        while (written < (size_t)nread) {
            if (off_out) {
                nwritten = pwrite_impl(fd_out, buffer + written,
                                       (size_t)nread - written, *off_out);
            } else {
                nwritten = write_impl(fd_out, buffer + written, (size_t)nread - written);
            }
            if (nwritten < 0) {
                return copied > 0 ? (ssize_t)copied : nwritten;
            }
            if (nwritten == 0) {
                return copied > 0 ? (ssize_t)copied : -EIO;
            }
            if (off_out) {
                *off_out += (int64_t)nwritten;
            }
            written += (size_t)nwritten;
            copied += (size_t)nwritten;
        }
        if (off_in) {
            *off_in += (int64_t)nread;
        }
    }

    return (ssize_t)copied;
}

int fallocate_impl(int fd, int mode, int64_t offset, int64_t len) {
    void *entry;
    int real_fd;
    struct stat st;
    int64_t end;

    if (mode != 0) {
        return -EOPNOTSUPP;
    }
    if (offset < 0 || len < 0) {
        return -EINVAL;
    }
    if (len == 0) {
        return 0;
    }
    if (offset > ((__LONG_LONG_MAX__) - len)) {
        return -EFBIG;
    }

    entry = get_fd_entry_impl(fd);
    if (!entry) {
        return -EBADF;
    }
    if (!get_fd_is_writable_impl(entry)) {
        put_fd_entry_impl(entry);
        return -EBADF;
    }
    if (get_fd_is_pipe_impl(entry) || get_fd_is_synthetic_dir_impl(entry)) {
        put_fd_entry_impl(entry);
        return -EINVAL;
    }
    real_fd = get_real_fd_impl(entry);
    put_fd_entry_impl(entry);
    if (real_fd < 0) {
        return -EINVAL;
    }
    if (backing_fstat(real_fd, &st) != 0) {
        return -1;
    }

    end = offset + len;
    if ((int64_t)st.st_size >= end) {
        return 0;
    }
    return ftruncate_impl(fd, end);
}

int sync_file_range_impl(int fd, int64_t offset, int64_t nbytes, unsigned int flags) {
    unsigned int allowed = SYNC_FILE_RANGE_WAIT_BEFORE | SYNC_FILE_RANGE_WRITE | SYNC_FILE_RANGE_WAIT_AFTER;

    if (offset < 0 || nbytes < 0) {
        return -EINVAL;
    }
    if ((flags & ~allowed) != 0) {
        return -EINVAL;
    }
    return fdatasync_impl(fd);
}

ssize_t splice_impl(int fd_in, int64_t *off_in, int fd_out, int64_t *off_out,
                    size_t len, unsigned int flags) {
    void *entry_in;
    void *entry_out;
    char buffer[16384];
    size_t copied = 0;

    if (flags != 0) {
        return -EINVAL;
    }
    if (len == 0) {
        return 0;
    }
    entry_in = get_fd_entry_impl(fd_in);
    if (!entry_in) {
        return -EBADF;
    }
    entry_out = get_fd_entry_impl(fd_out);
    if (!entry_out) {
        put_fd_entry_impl(entry_in);
        return -EBADF;
    }
    put_fd_entry_impl(entry_out);
    put_fd_entry_impl(entry_in);

    while (copied < len) {
        size_t chunk = len - copied;
        ssize_t nread;
        ssize_t nwritten;
        size_t written = 0;

        if (chunk > sizeof(buffer)) {
            chunk = sizeof(buffer);
        }
        if (off_in) {
            nread = pread_impl(fd_in, buffer, chunk, *off_in);
        } else {
            nread = read_impl(fd_in, buffer, chunk);
        }
        if (nread < 0) {
            return copied > 0 ? (ssize_t)copied : nread;
        }
        if (nread == 0) {
            break;
        }
        while (written < (size_t)nread) {
            if (off_out) {
                nwritten = pwrite_impl(fd_out, buffer + written, (size_t)nread - written, *off_out);
            } else {
                nwritten = write_impl(fd_out, buffer + written, (size_t)nread - written);
            }
            if (nwritten < 0) {
                return copied > 0 ? (ssize_t)copied : nwritten;
            }
            if (nwritten == 0) {
                return copied > 0 ? (ssize_t)copied : -EIO;
            }
            written += (size_t)nwritten;
            copied += (size_t)nwritten;
            if (off_out) {
                *off_out += (int64_t)nwritten;
            }
        }
        if (off_in) {
            *off_in += (int64_t)nread;
        }
    }

    return (ssize_t)copied;
}

ssize_t vmsplice_impl(int fd, const struct iovec *iov, unsigned long nr_segs, unsigned int flags) {
    size_t total = 0;

    if (flags != 0) {
        return -EINVAL;
    }
    if (!iov && nr_segs > 0) {
        return -EFAULT;
    }

    for (unsigned long i = 0; i < nr_segs; i++) {
        ssize_t nwritten;

        if (!iov[i].iov_base && iov[i].iov_len != 0) {
            return total > 0 ? (ssize_t)total : -EFAULT;
        }
        nwritten = write_impl(fd, iov[i].iov_base, iov[i].iov_len);
        if (nwritten < 0) {
            return total > 0 ? (ssize_t)total : nwritten;
        }
        total += (size_t)nwritten;
        if ((size_t)nwritten < iov[i].iov_len) {
            break;
        }
    }

    return (ssize_t)total;
}

ssize_t tee_impl(int fd_in, int fd_out, size_t len, unsigned int flags) {
    struct pipe_endpoint *src;
    struct pipe_endpoint *dst;
    ssize_t written;
    bool nonblock;
    void *entry;

    if (flags != 0) {
        return -EINVAL;
    }
    entry = get_fd_entry_impl(fd_in);
    if (!entry) {
        return -EBADF;
    }
    if (!get_fd_is_pipe_impl(entry)) {
        put_fd_entry_impl(entry);
        return -EINVAL;
    }
    src = get_fd_pipe_endpoint_impl(entry);
    put_fd_entry_impl(entry);
    if (!src) {
        return -EBADF;
    }

    entry = get_fd_entry_impl(fd_out);
    if (!entry) {
        return -EBADF;
    }
    if (!get_fd_is_pipe_impl(entry)) {
        put_fd_entry_impl(entry);
        return -EINVAL;
    }
    dst = get_fd_pipe_endpoint_impl(entry);
    nonblock = (get_fd_flags_impl(entry) & O_NONBLOCK) != 0;
    put_fd_entry_impl(entry);
    if (!dst) {
        return -EBADF;
    }
    written = pipe_tee_between_endpoints_impl(src, dst, len, nonblock);
    return written;
}

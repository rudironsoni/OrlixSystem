/* IXLandSystem/fs/readdir.c
 * Virtual getdents/getdents64 implementation
 */

#include <dirent.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "fdtable.h"
#include "internal/ios/fs/file_io_host.h"
#include "path.h"
#include "pty.h"
#include "vfs.h"
#include "kernel/cgroup.h"
#include "kernel/task.h"

struct linux_dirent64 {
    uint64_t d_ino;
    int64_t d_off;
    unsigned short d_reclen;
    unsigned char d_type;
    char d_name[];
};

static unsigned char map_dtype(unsigned char dtype) {
    switch (dtype) {
    case DT_FIFO:
        return DT_FIFO;
    case DT_CHR:
        return DT_CHR;
    case DT_DIR:
        return DT_DIR;
    case DT_BLK:
        return DT_BLK;
    case DT_REG:
        return DT_REG;
    case DT_LNK:
        return DT_LNK;
    case DT_SOCK:
        return DT_SOCK;
#ifdef DT_WHT
    case DT_WHT:
        return DT_WHT;
#endif
    default:
        return DT_UNKNOWN;
    }
}

static int append_linux_dirent64(void *dirp, size_t count, size_t *written, uint64_t ino,
                                 int64_t off, unsigned char type, const char *name) {
    size_t name_len = strlen(name);
    size_t record_len = sizeof(struct linux_dirent64) + name_len + 1;
    size_t aligned_len = (record_len + 7U) & ~7U;

    if (aligned_len > count - *written) {
        return 0;
    }

    struct linux_dirent64 *out = (struct linux_dirent64 *)((char *)dirp + *written);
    out->d_ino = ino;
    out->d_off = off;
    out->d_reclen = (unsigned short)aligned_len;
    out->d_type = type;
    memcpy(out->d_name, name, name_len + 1);

    if (aligned_len > record_len) {
        memset(((char *)out) + record_len, 0, aligned_len - record_len);
    }

    *written += aligned_len;
    return 1;
}

static ssize_t synthetic_getdents64(fd_entry_t *entry, void *dirp, size_t count) {
    linux_off_t cursor = get_fd_offset_impl(entry);
    if (cursor < 0) {
        cursor = 0;
    }

    /* Check unsupported class BEFORE writing any records */
    synthetic_dir_class_t dir_class = get_fd_synthetic_dir_class_impl(entry);
    if (dir_class == SYNTHETIC_DIR_GENERIC) {
        errno = ENOTSUP;
        return -1;
    }

    size_t written = 0;
    int rc;

    /* Write "." entry if at cursor 0 */
    if (cursor == 0) {
        rc = append_linux_dirent64(dirp, count, &written, 1, 1, DT_DIR, ".");
        if (rc == 0) {
            goto done;
        }
        cursor = 1;
    }

    /* Write ".." entry if at cursor 1 */
    if (cursor == 1) {
        rc = append_linux_dirent64(dirp, count, &written, 1, 2, DT_DIR, "..");
        if (rc == 0) {
            goto done;
        }
        cursor = 2;
    }

    if (dir_class == SYNTHETIC_DIR_CGROUPFS) {
        char dir_path[MAX_PATH];
        size_t idx = (size_t)(cursor - 2);

        if (get_fd_path_impl(entry, dir_path, sizeof(dir_path)) != 0) {
            errno = ENOENT;
            return -1;
        }
        if (strcmp(dir_path, "/sys/fs") == 0) {
            if (idx == 0) {
                rc = append_linux_dirent64(dirp, count, &written, 1, 3, DT_DIR, "cgroup");
                if (rc != 0) {
                    cursor++;
                }
            }
            goto done;
        }
        while (idx < cgroupfs_child_count(dir_path)) {
            char name[256];
            enum cgroupfs_node_type node_type;
            char child_path[MAX_PATH];
            unsigned char dtype = DT_REG;
            int ret;

            ret = cgroupfs_child_at(dir_path, idx, name, sizeof(name));
            if (ret != 0) {
                break;
            }
            ret = snprintf(child_path, sizeof(child_path), "%s/%s", dir_path, name);
            if (ret >= 0 && (size_t)ret < sizeof(child_path)) {
                node_type = cgroupfs_classify_path(child_path);
                if (node_type == CGROUPFS_NODE_DIR) {
                    dtype = DT_DIR;
                }
            }
            rc = append_linux_dirent64(dirp, count, &written, 1, (int64_t)(idx + 3), dtype, name);
            if (rc == 0) {
                break;
            }
            idx++;
            cursor++;
        }
        goto done;
    }

    if (dir_class == SYNTHETIC_DIR_PROC_SELF) {
        static const struct {
            const char *name;
            unsigned char dtype;
        } entries[] = {
            {"fd", DT_DIR},
            {"cwd", DT_LNK},
            {"exe", DT_LNK},
            {"cmdline", DT_REG},
            {"environ", DT_REG},
            {"comm", DT_REG},
            {"stat", DT_REG},
            {"statm", DT_REG},
            {"maps", DT_REG},
            {"status", DT_REG},
            {"cgroup", DT_REG},
            {"uid_map", DT_REG},
            {"gid_map", DT_REG},
            {"setgroups", DT_REG},
            {"mountinfo", DT_REG},
            {"mounts", DT_REG},
            {"fdinfo", DT_DIR},
            {"ns", DT_DIR},
            {"task", DT_DIR},
        };

        size_t num_entries = sizeof(entries) / sizeof(entries[0]);
        size_t idx = (size_t)(cursor - 2);

        while (idx < num_entries) {
            rc = append_linux_dirent64(dirp, count, &written, 1, (int64_t)(idx + 3),
                                       entries[idx].dtype, entries[idx].name);
            if (rc == 0) {
                break;
            }
            idx++;
            cursor++;
        }
    }

    if (dir_class == SYNTHETIC_DIR_PROC) {
        static const struct {
            const char *name;
            unsigned char dtype;
        } entries[] = {
            {"self", DT_LNK},
            {"filesystems", DT_REG},
            {"meminfo", DT_REG},
            {"cpuinfo", DT_REG},
        };
        size_t num_entries = sizeof(entries) / sizeof(entries[0]);
        size_t idx = (size_t)(cursor - 2);

        while (idx < num_entries) {
            rc = append_linux_dirent64(dirp, count, &written, 1, (int64_t)(idx + 3),
                                       entries[idx].dtype, entries[idx].name);
            if (rc == 0) {
                break;
            }
            idx++;
            cursor++;
        }
    }

    if (dir_class == SYNTHETIC_DIR_PROC_SELF_NS) {
        static const struct {
            const char *name;
            unsigned char dtype;
        } entries[] = {
            {"mnt", DT_LNK},
            {"uts", DT_LNK},
            {"pid", DT_LNK},
            {"cgroup", DT_LNK},
        };
        size_t num_entries = sizeof(entries) / sizeof(entries[0]);
        size_t idx = (size_t)(cursor - 2);

        while (idx < num_entries) {
            rc = append_linux_dirent64(dirp, count, &written, 1, (int64_t)(idx + 3),
                                       entries[idx].dtype, entries[idx].name);
            if (rc == 0) {
                break;
            }
            idx++;
            cursor++;
        }
    }

    if (dir_class == SYNTHETIC_DIR_PROC_SELF_TASK) {
        char dir_path[MAX_PATH];
        struct task_struct *target = NULL;
        int target_pid;
        int scan_pid = (cursor >= 2) ? ((int)cursor - 2) : 0;

        if (get_fd_path_impl(entry, dir_path, sizeof(dir_path)) != 0) {
            errno = ENOENT;
            return -1;
        }
        target_pid = vfs_proc_target_pid_for_path(dir_path);
        target = task_lookup(target_pid);
        if (!target) {
            errno = ENOENT;
            return -1;
        }

        kernel_mutex_lock(&task_table_lock);
        for (int bucket = 0; bucket < TASK_MAX_TASKS; bucket++) {
            struct task_struct *task = task_table[bucket];

            while (task) {
                if (task->tgid == target->tgid && task->pid >= scan_pid) {
                    char name[16];
                    int ret = snprintf(name, sizeof(name), "%d", task->pid);
                    if (ret < 0 || (size_t)ret >= sizeof(name)) {
                        kernel_mutex_unlock(&task_table_lock);
                        free_task(target);
                        errno = ENAMETOOLONG;
                        return -1;
                    }
                    rc = append_linux_dirent64(dirp, count, &written, 1,
                                               (int64_t)(task->pid + 2), DT_DIR, name);
                    if (rc == 0) {
                        kernel_mutex_unlock(&task_table_lock);
                        free_task(target);
                        goto done;
                    }
                    cursor = task->pid + 1;
                    scan_pid = task->pid + 1;
                }
                task = task->hash_next;
            }
        }
        kernel_mutex_unlock(&task_table_lock);
        free_task(target);
        goto done;
    }

    if (dir_class == SYNTHETIC_DIR_DEV) {
        static const struct {
            const char *name;
            unsigned char dtype;
        } entries[] = {
            {"null", DT_CHR},
            {"zero", DT_CHR},
            {"random", DT_CHR},
            {"urandom", DT_CHR},
            {"tty", DT_CHR},
            {"ptmx", DT_CHR},
            {"pts", DT_DIR},
        };
        size_t num_entries = sizeof(entries) / sizeof(entries[0]);
        size_t idx = (size_t)(cursor - 2);

        while (idx < num_entries) {
            rc = append_linux_dirent64(dirp, count, &written, 1, (int64_t)(idx + 3),
                                       entries[idx].dtype, entries[idx].name);
            if (rc == 0) {
                break;
            }
            idx++;
            cursor++;
        }
    }

    if (dir_class == SYNTHETIC_DIR_DEV_PTS) {
        unsigned int pty_indices[256];
        size_t num_entries = pty_list_slave_indices_impl(pty_indices, sizeof(pty_indices) / sizeof(pty_indices[0]));
        size_t idx = (size_t)(cursor - 2);

        while (idx < num_entries && idx < sizeof(pty_indices) / sizeof(pty_indices[0])) {
            char name[16];
            unsigned int value = pty_indices[idx];
            size_t len = 0;
            char digits[16];
            do {
                digits[len++] = (char)('0' + (value % 10));
                value /= 10;
            } while (value != 0 && len < sizeof(digits));

            for (size_t i = 0; i < len; i++) {
                name[i] = digits[len - 1 - i];
            }
            name[len] = '\0';

            rc = append_linux_dirent64(dirp, count, &written, 1, (int64_t)(idx + 3), DT_CHR, name);
            if (rc == 0) {
                break;
            }
            idx++;
            cursor++;
        }
    }

    /* SYNTHETIC_DIR_GENERIC and SYNTHETIC_DIR_PROC_SELF_FD* handled above */

    if (dir_class == SYNTHETIC_DIR_PROC_SELF_FD || dir_class == SYNTHETIC_DIR_PROC_SELF_FDINFO) {
        int scan_fd = (cursor >= 2) ? ((int)cursor - 2) : 0;
        char proc_dir_path[MAX_PATH];
        bool has_proc_dir_path = get_fd_path_impl(entry, proc_dir_path, sizeof(proc_dir_path)) == 0;

        for (; scan_fd < NR_OPEN_DEFAULT; scan_fd++) {
            bool fd_exists = has_proc_dir_path ? vfs_proc_fd_exists_for_path(proc_dir_path, scan_fd)
                                               : fdtable_is_used_impl(scan_fd);
            if (!fd_exists) {
                continue;
            }

            char fd_name[12];
            int fd_name_len = 0;
            {
                int n = scan_fd;
                char tmp[12];
                int pos = 0;
                if (n == 0) {
                    tmp[pos++] = '0';
                } else {
                    while (n > 0) {
                        tmp[pos++] = (char)('0' + (n % 10));
                        n /= 10;
                    }
                }
                for (int j = 0; j < pos; j++) {
                    fd_name[j] = tmp[pos - 1 - j];
                }
                fd_name_len = pos;
                fd_name[fd_name_len] = '\0';
            }

            unsigned char dtype = (dir_class == SYNTHETIC_DIR_PROC_SELF_FD) ? DT_LNK : DT_REG;
            rc = append_linux_dirent64(dirp, count, &written, 1, (int64_t)(scan_fd + 1), dtype, fd_name);
            if (rc == 0) {
                break;
            }
            cursor = scan_fd + 1;
        }
    }

done:
    set_fd_offset_impl(entry, cursor);
    return (ssize_t)written;
}

ssize_t getdents64_impl(int fd, void *dirp, size_t count) {
    if (dirp == NULL) {
        errno = EFAULT;
        return -1;
    }

    if (count == 0) {
        errno = EINVAL;
        return -1;
    }

    fd_entry_t *entry = get_fd_entry_impl(fd);
    if (entry == NULL) {
        errno = EBADF;
        return -1;
    }

    int real_fd = get_real_fd_impl(entry);
    linux_off_t saved_offset = get_fd_offset_impl(entry);
    bool is_dir = get_fd_is_dir_impl(entry);
    char fd_path[MAX_PATH];

    if (get_fd_is_pipe_impl(entry)) {
        put_fd_entry_impl(entry);
        errno = ENOTDIR;
        return -1;
    }

    if (is_dir && get_fd_path_impl(entry, fd_path, sizeof(fd_path)) == 0 &&
        vfs_path_is_synthetic(fd_path) && !get_fd_is_synthetic_dir_impl(entry)) {
        put_fd_entry_impl(entry);
        errno = ENOTSUP;
        return -1;
    }

    if (get_fd_is_synthetic_dir_impl(entry)) {
        ssize_t ret = synthetic_getdents64(entry, dirp, count);
        put_fd_entry_impl(entry);
        return ret;
    }

    int dup_fd = host_dup_impl(real_fd);
    if (dup_fd < 0) {
        put_fd_entry_impl(entry);
        return -1;
    }

    DIR *dp = fdopendir(dup_fd);
    if (dp == NULL) {
        int saved_errno = errno;
        host_close_impl(dup_fd);
        put_fd_entry_impl(entry);
        errno = saved_errno;
        return -1;
    }

    if (saved_offset > 0) {
        seekdir(dp, (long)saved_offset);
    }

    size_t written = 0;
    linux_off_t latest_offset = saved_offset;
    errno = 0;

    while (true) {
        struct dirent *native = readdir(dp);
        if (native == NULL) {
            if (errno != 0 && written == 0) {
                int saved_errno = errno;
                closedir(dp);
                host_close_impl(dup_fd);
                put_fd_entry_impl(entry);
                errno = saved_errno;
                return -1;
            }
            break;
        }

        size_t name_len = strlen(native->d_name);
        size_t record_len = sizeof(struct linux_dirent64) + name_len + 1;
        size_t aligned_len = (record_len + 7U) & ~7U;

        if (aligned_len > count - written) {
            if (written == 0) {
                closedir(dp);
                host_close_impl(dup_fd);
                put_fd_entry_impl(entry);
                errno = EINVAL;
                return -1;
            }
            break;
        }

        struct linux_dirent64 *out = (struct linux_dirent64 *)((char *)dirp + written);
        out->d_ino = native->d_ino;
        latest_offset = (linux_off_t)telldir(dp);
        out->d_off = latest_offset;
        out->d_reclen = (unsigned short)aligned_len;
        out->d_type = map_dtype(native->d_type);
        memcpy(out->d_name, native->d_name, name_len + 1);

        if (aligned_len > record_len) {
            memset(((char *)out) + record_len, 0, aligned_len - record_len);
        }

        written += aligned_len;
    }

    set_fd_offset_impl(entry, latest_offset);
    closedir(dp);
    /* closedir closes dup_fd; do NOT call host_close_impl here */
    put_fd_entry_impl(entry);
    return (ssize_t)written;
}

ssize_t getdents_impl(int fd, void *dirp, size_t count) {
    return getdents64_impl(fd, dirp, count);
}

__attribute__((visibility("default"))) ssize_t getdents(int fd, void *dirp, size_t count) {
    return getdents_impl(fd, dirp, count);
}

__attribute__((visibility("default"))) ssize_t getdents64(int fd, void *dirp, size_t count) {
    return getdents64_impl(fd, dirp, count);
}

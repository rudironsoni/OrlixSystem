/* OrlixKernel/fs/readdir.c
 * Virtual getdents/getdents64 implementation
 */

#include <linux/errno.h>
#include <linux/stdarg.h>
#include <linux/types.h>
#include <linux/dirent.h>
#include <linux/sprintf.h>
#include <linux/string.h>

#include "internal/fs/readdir.h"
#include "fdtable.h"
#include "internal/fs/file.h"
#include "path.h"
#include "pty.h"
#include "vfs.h"
#include "kernel/cgroup.h"
#include "kernel/task.h"
#include "../private/kernel/task_state.h"

#define LINUX_DT_UNKNOWN 0
#define LINUX_DT_FIFO 1
#define LINUX_DT_CHR 2
#define LINUX_DT_DIR 4
#define LINUX_DT_BLK 6
#define LINUX_DT_REG 8
#define LINUX_DT_LNK 10
#define LINUX_DT_SOCK 12

static unsigned char map_dtype(unsigned char dtype) {
    switch (dtype) {
    case LINUX_DT_FIFO:
        return LINUX_DT_FIFO;
    case LINUX_DT_CHR:
        return LINUX_DT_CHR;
    case LINUX_DT_DIR:
        return LINUX_DT_DIR;
    case LINUX_DT_BLK:
        return LINUX_DT_BLK;
    case LINUX_DT_REG:
        return LINUX_DT_REG;
    case LINUX_DT_LNK:
        return LINUX_DT_LNK;
    case LINUX_DT_SOCK:
        return LINUX_DT_SOCK;
    default:
        return LINUX_DT_UNKNOWN;
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
    int64_t cursor = get_fd_offset_impl(entry);
    if (cursor < 0) {
        cursor = 0;
    }

    /* Check unsupported class BEFORE writing any records */
    synthetic_dir_class_t dir_class = get_fd_synthetic_dir_class_impl(entry);
    if (dir_class == SYNTHETIC_DIR_GENERIC) {
        return -EOPNOTSUPP;
    }

    size_t written = 0;
    int rc;

    /* Write "." entry if at cursor 0 */
    if (cursor == 0) {
        rc = append_linux_dirent64(dirp, count, &written, 1, 1, LINUX_DT_DIR, ".");
        if (rc == 0) {
            goto done;
        }
        cursor = 1;
    }

    /* Write ".." entry if at cursor 1 */
    if (cursor == 1) {
        rc = append_linux_dirent64(dirp, count, &written, 1, 2, LINUX_DT_DIR, "..");
        if (rc == 0) {
            goto done;
        }
        cursor = 2;
    }

    if (dir_class == SYNTHETIC_DIR_CGROUPFS) {
        char dir_path[MAX_PATH];
        size_t idx = (size_t)(cursor - 2);

        if (get_fd_path_impl(entry, dir_path, sizeof(dir_path)) != 0) {
            return -ENOENT;
        }
        if (strcmp(dir_path, "/sys/fs") == 0) {
            if (idx == 0) {
                rc = append_linux_dirent64(dirp, count, &written, 1, 3, LINUX_DT_DIR, "cgroup");
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
            unsigned char dtype = LINUX_DT_REG;
            int ret;

            ret = cgroupfs_child_at(dir_path, idx, name, sizeof(name));
            if (ret != 0) {
                break;
            }
            ret = snprintf(child_path, sizeof(child_path), "%s/%s", dir_path, name);
            if (ret >= 0 && (size_t)ret < sizeof(child_path)) {
                node_type = cgroupfs_classify_path(child_path);
                if (node_type == CGROUPFS_NODE_DIR) {
                    dtype = LINUX_DT_DIR;
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
            {"fd", LINUX_DT_DIR},
            {"cwd", LINUX_DT_LNK},
            {"exe", LINUX_DT_LNK},
            {"cmdline", LINUX_DT_REG},
            {"environ", LINUX_DT_REG},
            {"comm", LINUX_DT_REG},
            {"stat", LINUX_DT_REG},
            {"statm", LINUX_DT_REG},
            {"maps", LINUX_DT_REG},
            {"status", LINUX_DT_REG},
            {"cgroup", LINUX_DT_REG},
            {"uid_map", LINUX_DT_REG},
            {"gid_map", LINUX_DT_REG},
            {"setgroups", LINUX_DT_REG},
            {"mountinfo", LINUX_DT_REG},
            {"mounts", LINUX_DT_REG},
            {"fdinfo", LINUX_DT_DIR},
            {"ns", LINUX_DT_DIR},
            {"task", LINUX_DT_DIR},
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
            {"self", LINUX_DT_LNK},
            {"filesystems", LINUX_DT_REG},
            {"meminfo", LINUX_DT_REG},
            {"cpuinfo", LINUX_DT_REG},
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
            {"mnt", LINUX_DT_LNK},
            {"uts", LINUX_DT_LNK},
            {"pid", LINUX_DT_LNK},
            {"cgroup", LINUX_DT_LNK},
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
        struct task *target = NULL;
        int target_pid;
        int scan_pid = (cursor >= 2) ? ((int)cursor - 2) : 0;

        if (get_fd_path_impl(entry, dir_path, sizeof(dir_path)) != 0) {
            return -ENOENT;
        }
        target_pid = vfs_proc_target_pid_for_path(dir_path);
        target = task_lookup(target_pid);
        if (!target) {
            return -ENOENT;
        }

        kernel_mutex_lock(&task_table_lock);
        for (int bucket = 0; bucket < TASK_MAX_TASKS; bucket++) {
            struct task *task = task_table[bucket];

            while (task) {
                if (task->tgid == target->tgid && task->pid >= scan_pid) {
                    char name[16];
                    int ret = snprintf(name, sizeof(name), "%d", task->pid);
                    if (ret < 0 || (size_t)ret >= sizeof(name)) {
                        kernel_mutex_unlock(&task_table_lock);
                        task_put(target);
                        return -ENAMETOOLONG;
                    }
                    rc = append_linux_dirent64(dirp, count, &written, 1,
                                               (int64_t)(task->pid + 2), LINUX_DT_DIR, name);
                    if (rc == 0) {
                        kernel_mutex_unlock(&task_table_lock);
                        task_put(target);
                        goto done;
                    }
                    cursor = task->pid + 1;
                    scan_pid = task->pid + 1;
                }
                task = task->hash_next;
            }
        }
        kernel_mutex_unlock(&task_table_lock);
        task_put(target);
        goto done;
    }

    if (dir_class == SYNTHETIC_DIR_DEV) {
        static const struct {
            const char *name;
            unsigned char dtype;
        } entries[] = {
            {"null", LINUX_DT_CHR},
            {"zero", LINUX_DT_CHR},
            {"random", LINUX_DT_CHR},
            {"urandom", LINUX_DT_CHR},
            {"tty", LINUX_DT_CHR},
            {"ptmx", LINUX_DT_CHR},
            {"pts", LINUX_DT_DIR},
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

            rc = append_linux_dirent64(dirp, count, &written, 1, (int64_t)(idx + 3), LINUX_DT_CHR, name);
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

            unsigned char dtype = (dir_class == SYNTHETIC_DIR_PROC_SELF_FD) ? LINUX_DT_LNK : LINUX_DT_REG;
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
        return -EFAULT;
    }

    if (count == 0) {
        return -EINVAL;
    }

    fd_entry_t *entry = get_fd_entry_impl(fd);
    if (entry == NULL) {
        return -EBADF;
    }

    int real_fd = get_real_fd_impl(entry);
    int64_t saved_offset = get_fd_offset_impl(entry);
    bool is_dir = get_fd_is_dir_impl(entry);
    char fd_path[MAX_PATH];

    if (get_fd_is_pipe_impl(entry)) {
        put_fd_entry_impl(entry);
        return -ENOTDIR;
    }

    if (is_dir && get_fd_path_impl(entry, fd_path, sizeof(fd_path)) == 0 &&
        vfs_path_is_synthetic(fd_path) && !get_fd_is_synthetic_dir_impl(entry)) {
        put_fd_entry_impl(entry);
        return -EOPNOTSUPP;
    }

    if (get_fd_is_synthetic_dir_impl(entry)) {
        ssize_t ret = synthetic_getdents64(entry, dirp, count);
        put_fd_entry_impl(entry);
        return ret;
    }

    struct backing_dir_stream *stream = NULL;
    size_t written = 0;
    int64_t latest_offset = saved_offset;
    struct backing_dir_record native;

    int open_result = backing_dir_open(real_fd, saved_offset, &stream);
    if (open_result < 0) {
        put_fd_entry_impl(entry);
        return open_result;
    }

    while (true) {
        int read_result = backing_dir_read(stream, &native);
        if (read_result <= 0) {
            if (read_result < 0 && written == 0) {
                backing_dir_close(stream);
                put_fd_entry_impl(entry);
                return read_result;
            }
            break;
        }

        size_t name_len = strlen(native.name);
        size_t record_len = sizeof(struct linux_dirent64) + name_len + 1;
        size_t aligned_len = (record_len + 7U) & ~7U;

        if (aligned_len > count - written) {
            if (written == 0) {
                backing_dir_close(stream);
                put_fd_entry_impl(entry);
                return -EINVAL;
            }
            break;
        }

        struct linux_dirent64 *out = (struct linux_dirent64 *)((char *)dirp + written);
        out->d_ino = native.ino;
        latest_offset = native.off;
        out->d_off = latest_offset;
        out->d_reclen = (unsigned short)aligned_len;
        out->d_type = map_dtype(native.type);
        memcpy(out->d_name, native.name, name_len + 1);

        if (aligned_len > record_len) {
            memset(((char *)out) + record_len, 0, aligned_len - record_len);
        }

        written += aligned_len;
    }

    set_fd_offset_impl(entry, latest_offset);
    backing_dir_close(stream);
    put_fd_entry_impl(entry);
    return (ssize_t)written;
}

ssize_t getdents_impl(int fd, void *dirp, size_t count) {
    return getdents64_impl(fd, dirp, count);
}

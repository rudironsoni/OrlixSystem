#include <linux/fcntl.h>
#include <linux/stat.h>

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "fs/fdtable.h"
#include "fs/pty.h"
#include "fs/vfs.h"
#include "kernel/signal.h"
#include "kernel/task.h"

extern int open_impl(const char *pathname, int flags, linux_mode_t mode);
extern int close_impl(int fd);
extern long read_impl(int fd, void *buf, size_t count);
extern ssize_t getdents64(int fd, void *dirp, size_t count);
extern int stat_impl(const char *pathname, struct linux_stat *statbuf);
extern int access(const char *pathname, int mode);

struct devfs_linux_dirent64 {
    uint64_t d_ino;
    int64_t d_off;
    unsigned short d_reclen;
    unsigned char d_type;
    char d_name[];
};

static int close_if_open(int fd) {
    if (fd >= 0) {
        return close_impl(fd);
    }
    return 0;
}

static bool dirent_buffer_contains(const char *buf, ssize_t nread, const char *name) {
    size_t pos = 0;
    while (pos < (size_t)nread) {
        const struct devfs_linux_dirent64 *entry = (const struct devfs_linux_dirent64 *)(buf + pos);
        if (entry->d_reclen == 0 || entry->d_reclen > (unsigned short)((size_t)nread - pos)) {
            return false;
        }
        if (strcmp(entry->d_name, name) == 0) {
            return true;
        }
        pos += entry->d_reclen;
    }
    return false;
}

static int append_decimal(char *buf, size_t buf_size, unsigned int value) {
    char digits[16];
    size_t count = 0;

    do {
        digits[count++] = (char)('0' + (value % 10));
        value /= 10;
    } while (value != 0 && count < sizeof(digits));

    if (value != 0 || count + 1 > buf_size) {
        errno = ENAMETOOLONG;
        return -1;
    }

    for (size_t i = 0; i < count; i++) {
        buf[i] = digits[count - 1 - i];
    }
    buf[count] = '\0';
    return 0;
}

int devfs_contract_random_device_is_character_and_readable(void) {
    struct linux_stat st;
    unsigned char buf[64];
    int fd = -1;

    if (stat_impl("/dev/random", &st) != 0) {
        return -1;
    }
    if ((st.st_mode & S_IFMT) != S_IFCHR || (st.st_mode & 0777) != 0666) {
        errno = ERANGE;
        return -1;
    }
    if (access("/dev/random", F_OK) != 0) {
        return -1;
    }

    fd = open_impl("/dev/random", O_RDONLY, 0);
    if (fd < 0) {
        return -1;
    }
    memset(buf, 0, sizeof(buf));
    long nread = read_impl(fd, buf, sizeof(buf));
    close_if_open(fd);
    if (nread != (long)sizeof(buf)) {
        errno = ENODATA;
        return -1;
    }
    return 0;
}

int devfs_contract_tty_node_exists_without_controlling_tty(void) {
    struct linux_stat st;
    if (stat_impl("/dev/tty", &st) != 0) {
        return -1;
    }
    if ((st.st_mode & S_IFMT) != S_IFCHR || (st.st_mode & 0777) != 0666) {
        errno = ERANGE;
        return -1;
    }
    return access("/dev/tty", F_OK);
}

int devfs_contract_tty_open_without_controlling_tty_returns_enxio(void) {
    struct task_struct *original = get_current();
    struct task_struct *task = alloc_task();
    if (!task) {
        return -1;
    }

    task->fs = alloc_fs_struct();
    if (!task->fs) {
        free_task(task);
        return -1;
    }
    task->signal = alloc_signal_struct();
    if (!task->signal) {
        free_task(task);
        return -1;
    }
    fs_init_root(task->fs, "/");
    fs_init_pwd(task->fs, "/");

    set_current(task);
    errno = 0;
    int fd = open_impl("/dev/tty", O_RDWR, 0);
    int saved_errno = errno;
    if (fd >= 0) {
        close_if_open(fd);
    }
    set_current(original);
    free_task(task);

    if (fd != -1 || saved_errno != ENXIO) {
        errno = saved_errno ? saved_errno : ERANGE;
        return -1;
    }
    errno = ENXIO;
    return 0;
}

int devfs_contract_pts_directory_exists(void) {
    struct linux_stat st;
    int fd = -1;

    if (stat_impl("/dev/pts", &st) != 0) {
        return -1;
    }
    if ((st.st_mode & S_IFMT) != S_IFDIR) {
        errno = ENOTDIR;
        return -1;
    }
    if (access("/dev/pts", F_OK) != 0) {
        return -1;
    }

    fd = open_impl("/dev/pts", O_RDONLY | O_DIRECTORY, 0);
    if (fd < 0) {
        return -1;
    }
    close_if_open(fd);
    return 0;
}

int devfs_contract_dev_directory_lists_linux_device_nodes(void) {
    char buf[1024];
    int fd = open_impl("/dev", O_RDONLY | O_DIRECTORY, 0);
    if (fd < 0) {
        return -1;
    }

    ssize_t nread = getdents64(fd, buf, sizeof(buf));
    close_if_open(fd);
    if (nread <= 0) {
        errno = ENODATA;
        return -1;
    }

    if (!dirent_buffer_contains(buf, nread, "null") ||
        !dirent_buffer_contains(buf, nread, "zero") ||
        !dirent_buffer_contains(buf, nread, "random") ||
        !dirent_buffer_contains(buf, nread, "urandom") ||
        !dirent_buffer_contains(buf, nread, "tty") ||
        !dirent_buffer_contains(buf, nread, "ptmx") ||
        !dirent_buffer_contains(buf, nread, "pts")) {
        errno = ENOENT;
        return -1;
    }
    return 0;
}

int devfs_contract_allocated_pty_slave_is_visible_in_devpts(void) {
    unsigned int pty_index = 0;
    char slave_path[64];
    char name[16];
    char buf[1024];
    struct linux_stat st;

    if (pty_allocate_pair_impl(&pty_index) != 0) {
        return -1;
    }
    if (pty_set_lock_impl(pty_index, false) != 0) {
        pty_close_end_impl(pty_index, true);
        return -1;
    }
    if (pty_format_slave_path_impl(pty_index, slave_path, sizeof(slave_path)) != 0) {
        pty_close_end_impl(pty_index, true);
        return -1;
    }

    if (stat_impl(slave_path, &st) != 0) {
        pty_close_end_impl(pty_index, true);
        return -1;
    }
    if ((st.st_mode & S_IFMT) != S_IFCHR) {
        pty_close_end_impl(pty_index, true);
        errno = ERANGE;
        return -1;
    }

    if (append_decimal(name, sizeof(name), pty_index) != 0) {
        pty_close_end_impl(pty_index, true);
        return -1;
    }

    int dirfd = open_impl("/dev/pts", O_RDONLY | O_DIRECTORY, 0);
    if (dirfd < 0) {
        pty_close_end_impl(pty_index, true);
        return -1;
    }
    ssize_t nread = getdents64(dirfd, buf, sizeof(buf));
    close_if_open(dirfd);
    pty_close_end_impl(pty_index, true);

    if (nread <= 0 || !dirent_buffer_contains(buf, nread, name)) {
        errno = ENOENT;
        return -1;
    }
    return 0;
}

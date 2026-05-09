#include <asm/ioctls.h>
#include <linux/fcntl.h>
#define __ASSEMBLY__ 1
#include <asm-generic/signal.h>
#undef __ASSEMBLY__
#include <asm-generic/signal-defs.h>

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifndef S_IFMT
#define S_IFMT 0170000
#endif

#ifndef S_IFCHR
#define S_IFCHR 0020000
#endif

#include "fs/fdtable.h"
#include "fs/vfs.h"
#include "kernel/signal.h"
#include "kernel/task.h"

extern int open_impl(const char *pathname, int flags, uint32_t mode);
extern int close_impl(int fd);
extern int dup_impl(int oldfd);
extern int dup3_impl(int oldfd, int newfd, int flags);
extern int fcntl_impl(int fd, int cmd, ...);
extern long read_impl(int fd, void *buf, size_t count);
extern long write_impl(int fd, const void *buf, size_t count);
extern int fstat_impl(int fd, struct linux_stat *statbuf);
extern int readlink_impl(const char *pathname, char *buf, size_t bufsiz);
extern ssize_t getdents64(int fd, void *dirp, size_t count);
extern int32_t getpgrp_impl(void);
extern int32_t getsid_impl(int32_t pid);
extern int32_t setsid_impl(void);
extern void exit_impl(int status);
extern __kernel_pid_t tcgetsid(int fd);

extern int pty_contract_ioctl(int fd, unsigned long request, ...);

struct linux_dirent64 {
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

static void clear_pending_signal(struct task_struct *task, int32_t sig) {
    if (!task || !task->signal || sig <= 0 || sig > KERNEL_SIG_NUM) {
        return;
    }
    task->thread_pending_signals &= ~(1ULL << ((sig - 1) & 63));
    task->signal->shared_pending.sig[(sig - 1) >> 6] &= ~(1ULL << ((sig - 1) & 63));
}

static int append_decimal(char *buf, size_t buf_size, int value) {
    char digits[16];
    size_t count = 0;
    size_t i;

    if (value < 0) {
        errno = EINVAL;
        return -1;
    }

    do {
        digits[count++] = (char)('0' + (value % 10));
        value /= 10;
    } while (value > 0 && count < sizeof(digits));

    if (value > 0 || count + 1 > buf_size) {
        errno = ENAMETOOLONG;
        return -1;
    }

    for (i = 0; i < count; i++) {
        buf[i] = digits[count - 1 - i];
    }
    buf[count] = '\0';
    return 0;
}

static int dir_contains_name(int fd, const char *needle) {
    char buf[4096];
    ssize_t nread;
    size_t offset = 0;

    if (!needle) {
        errno = EINVAL;
        return -1;
    }

    nread = getdents64(fd, buf, sizeof(buf));
    if (nread < 0) {
        return -1;
    }

    while (offset < (size_t)nread) {
        struct linux_dirent64 *entry = (struct linux_dirent64 *)(buf + offset);
        if (strcmp(entry->d_name, needle) == 0) {
            return 1;
        }
        if (entry->d_reclen == 0) {
            break;
        }
        offset += entry->d_reclen;
    }

    return 0;
}

static int build_proc_fd_path(char *buf, size_t buf_size, const char *prefix, int fd_num) {
    size_t prefix_len;

    if (!buf || !prefix || fd_num < 0) {
        errno = EINVAL;
        return -1;
    }

    prefix_len = strlen(prefix);
    if (prefix_len >= buf_size) {
        errno = ENAMETOOLONG;
        return -1;
    }

    memcpy(buf, prefix, prefix_len);
    return append_decimal(buf + prefix_len, buf_size - prefix_len, fd_num);
}

static int build_pts_path(char *buf, size_t buf_size, unsigned int pty_index) {
    if (pty_index > 2147483647U) {
        errno = EINVAL;
        return -1;
    }
    return build_proc_fd_path(buf, buf_size, "/dev/pts/", (int)pty_index);
}

static int readlink_fd_target(int fd_num, char *buf, size_t buf_size) {
    char path[64];
    ssize_t link_len;

    if (!buf || buf_size == 0) {
        errno = EINVAL;
        return -1;
    }

    if (build_proc_fd_path(buf, 64, "/proc/self/fd/", fd_num) != 0) {
        return -1;
    }
    memcpy(path, buf, strlen(buf) + 1);

    link_len = readlink_impl(path, buf, buf_size - 1);
    if (link_len < 0) {
        return -1;
    }
    buf[link_len] = '\0';
    return 0;
}

static int detach_controlling_tty_if_present(void) {
    int tty_fd = open_impl("/dev/tty", O_RDWR, 0);

    if (tty_fd < 0) {
        if (errno == ENXIO || errno == EIO) {
            return 0;
        }
        return -1;
    }

    if (pty_contract_ioctl(tty_fd, TIOCNOTTY, 0) != 0) {
        close_impl(tty_fd);
        return -1;
    }

    return close_impl(tty_fd);
}

static int read_fdinfo_flags(int fd_num, unsigned int *flags_out) {
    static const char prefix[] = "/proc/self/fdinfo/";
    char path[64];
    char buf[256];
    int infofd;
    long nread;
    char *flags_line;
    char *value;
    unsigned long parsed;

    if (!flags_out) {
        errno = EINVAL;
        return -1;
    }

    if (strlen(prefix) + 16 >= sizeof(path)) {
        errno = ENAMETOOLONG;
        return -1;
    }

    memcpy(path, prefix, sizeof(prefix) - 1);
    if (append_decimal(path + sizeof(prefix) - 1, sizeof(path) - (sizeof(prefix) - 1), fd_num) != 0) {
        return -1;
    }

    infofd = open_impl(path, O_RDONLY, 0);
    if (infofd < 0) {
        return -1;
    }

    nread = read_impl(infofd, buf, sizeof(buf) - 1);
    close_impl(infofd);
    if (nread <= 0) {
        errno = EPROTO;
        return -1;
    }
    buf[nread] = '\0';

    flags_line = strstr(buf, "flags:\t");
    if (!flags_line) {
        errno = EPROTO;
        return -1;
    }

    value = flags_line + 7;
    parsed = 0;
    while (*value >= '0' && *value <= '7') {
        parsed = (parsed << 3) | (unsigned long)(*value - '0');
        value++;
    }

    *flags_out = (unsigned int)parsed;
    return 0;
}

static int alloc_pty_pair(int cloexec, int nonblock, int *master_fd_out, int *slave_fd_out, unsigned int *pty_index_out) {
    int master_fd = -1;
    int slave_fd = -1;
    unsigned int pty_index = 0;
    int unlock = 0;
    char slave_path[64];

    if (!master_fd_out || !slave_fd_out || !pty_index_out) {
        errno = EINVAL;
        return -1;
    }

    master_fd = open_impl("/dev/ptmx", O_RDWR | (cloexec ? O_CLOEXEC : 0) | (nonblock ? O_NONBLOCK : 0), 0);
    if (master_fd < 0) {
        return -1;
    }

    if (pty_contract_ioctl(master_fd, TIOCGPTN, &pty_index) != 0) {
        close_impl(master_fd);
        return -1;
    }

    unlock = 0;
    if (pty_contract_ioctl(master_fd, TIOCSPTLCK, &unlock) != 0) {
        close_impl(master_fd);
        return -1;
    }

    memcpy(slave_path, "/dev/pts/", 9);
    if (append_decimal(slave_path + 9, sizeof(slave_path) - 9, (int)pty_index) != 0) {
        close_impl(master_fd);
        return -1;
    }

    slave_fd = open_impl(slave_path, O_RDWR | (cloexec ? O_CLOEXEC : 0) | (nonblock ? O_NONBLOCK : 0), 0);
    if (slave_fd < 0) {
        close_impl(master_fd);
        return -1;
    }

    *master_fd_out = master_fd;
    *slave_fd_out = slave_fd;
    *pty_index_out = pty_index;
    return 0;
}

int pty_session_contract_boot_init_task_has_linux_session_identity(void) {
    struct task_struct *task = get_current();
    if (!task) {
        errno = ESRCH;
        return -1;
    }
    if (task->pid != 1 || task->tgid != 1 || task->pgid != 1 || task->sid != 1) {
        errno = EPROTO;
        return -1;
    }
    if (getpgrp_impl() != 1 || getsid_impl(0) != 1) {
        errno = EPROTO;
        return -1;
    }
    return 0;
}

int pty_session_contract_dev_tty_fails_without_controlling_terminal(void) {
    int fd = open_impl("/dev/tty", O_RDWR, 0);
    if (fd >= 0) {
        close_impl(fd);
        errno = EPROTO;
        return -1;
    }
    if (errno != ENXIO && errno != EIO) {
        errno = EPROTO;
        return -1;
    }
    return 0;
}

int pty_session_contract_pty_pair_allocates_master_and_slave_descriptors(void) {
    int master_fd = -1;
    int slave_fd = -1;
    unsigned int pty_index = 0;

    if (alloc_pty_pair(0, 0, &master_fd, &slave_fd, &pty_index) != 0) {
        return -1;
    }

    if (master_fd < 0 || slave_fd < 0 || master_fd == slave_fd) {
        close_if_open(master_fd);
        close_if_open(slave_fd);
        errno = EPROTO;
        return -1;
    }

    close_if_open(master_fd);
    close_if_open(slave_fd);
    return 0;
}

int pty_session_contract_pty_descriptors_appear_in_proc_self_fd(void) {
    int master_fd = -1;
    int slave_fd = -1;
    int proc_fd = -1;
    unsigned int pty_index = 0;
    char master_name[16];
    char slave_name[16];
    int has_master;
    int has_slave;

    if (alloc_pty_pair(0, 0, &master_fd, &slave_fd, &pty_index) != 0) {
        return -1;
    }

    if (append_decimal(master_name, sizeof(master_name), master_fd) != 0 ||
        append_decimal(slave_name, sizeof(slave_name), slave_fd) != 0) {
        close_if_open(master_fd);
        close_if_open(slave_fd);
        return -1;
    }

    proc_fd = open_impl("/proc/self/fd", O_RDONLY | O_DIRECTORY, 0);
    if (proc_fd < 0) {
        close_if_open(master_fd);
        close_if_open(slave_fd);
        return -1;
    }
    has_master = dir_contains_name(proc_fd, master_name);
    close_if_open(proc_fd);
    if (has_master < 0) {
        close_if_open(master_fd);
        close_if_open(slave_fd);
        return -1;
    }

    proc_fd = open_impl("/proc/self/fd", O_RDONLY | O_DIRECTORY, 0);
    if (proc_fd < 0) {
        close_if_open(master_fd);
        close_if_open(slave_fd);
        return -1;
    }
    has_slave = dir_contains_name(proc_fd, slave_name);
    close_if_open(proc_fd);
    if (has_slave < 0) {
        close_if_open(master_fd);
        close_if_open(slave_fd);
        return -1;
    }

    if (has_master != 1 || has_slave != 1) {
        close_if_open(master_fd);
        close_if_open(slave_fd);
        errno = EPROTO;
        return -1;
    }

    close_if_open(master_fd);
    close_if_open(slave_fd);
    return 0;
}

int pty_session_contract_pty_fstat_reports_character_device(void) {
    int master_fd = -1;
    int slave_fd = -1;
    unsigned int pty_index = 0;
    struct linux_stat st;

    if (alloc_pty_pair(0, 0, &master_fd, &slave_fd, &pty_index) != 0) {
        return -1;
    }

    if (fstat_impl(master_fd, &st) != 0 || (st.st_mode & S_IFMT) != S_IFCHR) {
        close_if_open(master_fd);
        close_if_open(slave_fd);
        errno = EPROTO;
        return -1;
    }
    if (fstat_impl(slave_fd, &st) != 0 || (st.st_mode & S_IFMT) != S_IFCHR) {
        close_if_open(master_fd);
        close_if_open(slave_fd);
        errno = EPROTO;
        return -1;
    }

    close_if_open(master_fd);
    close_if_open(slave_fd);
    return 0;
}

int pty_session_contract_pty_write_master_read_slave(void) {
    int master_fd = -1;
    int slave_fd = -1;
    unsigned int pty_index = 0;
    char buf[16];
    static const char payload[] = "ping\n";
    long nread;

    if (alloc_pty_pair(0, 0, &master_fd, &slave_fd, &pty_index) != 0) {
        return -1;
    }

    if (write_impl(master_fd, payload, sizeof(payload) - 1) != (long)(sizeof(payload) - 1)) {
        close_if_open(master_fd);
        close_if_open(slave_fd);
        return -1;
    }

    nread = read_impl(slave_fd, buf, sizeof(payload) - 1);
    if (nread != (long)(sizeof(payload) - 1) || memcmp(buf, payload, sizeof(payload) - 1) != 0) {
        close_if_open(master_fd);
        close_if_open(slave_fd);
        errno = EPROTO;
        return -1;
    }

    close_if_open(master_fd);
    close_if_open(slave_fd);
    return 0;
}

int pty_session_contract_pty_write_slave_read_master(void) {
    int master_fd = -1;
    int slave_fd = -1;
    unsigned int pty_index = 0;
    char buf[16];
    static const char payload[] = "pong";
    long nread;

    if (alloc_pty_pair(0, 0, &master_fd, &slave_fd, &pty_index) != 0) {
        return -1;
    }

    if (write_impl(slave_fd, payload, sizeof(payload) - 1) != (long)(sizeof(payload) - 1)) {
        close_if_open(master_fd);
        close_if_open(slave_fd);
        return -1;
    }

    nread = read_impl(master_fd, buf, sizeof(payload) - 1);
    if (nread != (long)(sizeof(payload) - 1) || memcmp(buf, payload, sizeof(payload) - 1) != 0) {
        close_if_open(master_fd);
        close_if_open(slave_fd);
        errno = EPROTO;
        return -1;
    }

    close_if_open(master_fd);
    close_if_open(slave_fd);
    return 0;
}

int pty_session_contract_pty_nonblocking_read_without_data_returns_again(void) {
    int master_fd = -1;
    int slave_fd = -1;
    unsigned int pty_index = 0;
    char byte;

    if (alloc_pty_pair(0, 1, &master_fd, &slave_fd, &pty_index) != 0) {
        return -1;
    }

    if (read_impl(master_fd, &byte, 1) != -1 || errno != EAGAIN) {
        close_if_open(master_fd);
        close_if_open(slave_fd);
        errno = EPROTO;
        return -1;
    }
    if (read_impl(slave_fd, &byte, 1) != -1 || errno != EAGAIN) {
        close_if_open(master_fd);
        close_if_open(slave_fd);
        errno = EPROTO;
        return -1;
    }

    close_if_open(master_fd);
    close_if_open(slave_fd);
    return 0;
}

int pty_session_contract_close_on_exec_closes_only_flagged_pty_descriptor(void) {
    int master_fd = -1;
    int slave_fd = -1;
    unsigned int pty_index = 0;
    int closed;

    if (alloc_pty_pair(1, 0, &master_fd, &slave_fd, &pty_index) != 0) {
        return -1;
    }

    if (fcntl_impl(master_fd, F_SETFD, 0) != 0) {
        close_if_open(master_fd);
        close_if_open(slave_fd);
        return -1;
    }

    closed = close_on_exec_impl();
    if (closed != 1) {
        close_if_open(master_fd);
        close_if_open(slave_fd);
        errno = EPROTO;
        return -1;
    }

    if (fcntl_impl(master_fd, F_GETFD, 0) < 0) {
        close_if_open(slave_fd);
        return -1;
    }
    if (fcntl_impl(slave_fd, F_GETFD, 0) != -1 || errno != EBADF) {
        close_if_open(master_fd);
        close_if_open(slave_fd);
        errno = EPROTO;
        return -1;
    }

    close_if_open(master_fd);
    return 0;
}

int pty_session_contract_controlling_tty_attach_makes_dev_tty_usable(void) {
    int master_fd = -1;
    int slave_fd = -1;
    int tty_fd = -1;
    unsigned int pty_index = 0;
    char tty_target[64];
    char expected_target[64];
    int32_t foreground_pgrp = 0;

    if (detach_controlling_tty_if_present() != 0) {
        return -1;
    }

    if (alloc_pty_pair(0, 0, &master_fd, &slave_fd, &pty_index) != 0) {
        return -1;
    }

    if (pty_contract_ioctl(slave_fd, TIOCSCTTY, 0) != 0) {
        close_if_open(master_fd);
        close_if_open(slave_fd);
        return -1;
    }

    tty_fd = open_impl("/dev/tty", O_RDWR, 0);
    if (tty_fd < 0) {
        close_if_open(master_fd);
        close_if_open(slave_fd);
        return -1;
    }

    if (readlink_fd_target(tty_fd, tty_target, sizeof(tty_target)) != 0) {
        close_if_open(tty_fd);
        close_if_open(master_fd);
        close_if_open(slave_fd);
        return -1;
    }
    if (build_pts_path(expected_target, sizeof(expected_target), pty_index) != 0) {
        close_if_open(tty_fd);
        close_if_open(master_fd);
        close_if_open(slave_fd);
        return -1;
    }
    if (strcmp(tty_target, expected_target) != 0) {
        close_if_open(tty_fd);
        close_if_open(master_fd);
        close_if_open(slave_fd);
        errno = EPROTO;
        return -1;
    }

    if (pty_contract_ioctl(tty_fd, TIOCGPGRP, &foreground_pgrp) != 0 || foreground_pgrp != 1) {
        close_if_open(tty_fd);
        close_if_open(master_fd);
        close_if_open(slave_fd);
        errno = EPROTO;
        return -1;
    }

    close_if_open(tty_fd);
    close_if_open(master_fd);
    close_if_open(slave_fd);
    return 0;
}

int pty_session_contract_tcgetsid_matches_controlling_session(void) {
    int master_fd = -1;
    int slave_fd = -1;
    int tty_fd = -1;
    unsigned int pty_index = 0;
    __kernel_pid_t tty_sid = -1;

    if (detach_controlling_tty_if_present() != 0) {
        return -1;
    }

    if (alloc_pty_pair(0, 0, &master_fd, &slave_fd, &pty_index) != 0) {
        return -1;
    }

    errno = 0;
    if (tcgetsid(slave_fd) != -1 || errno != ENOTTY) {
        close_if_open(master_fd);
        close_if_open(slave_fd);
        errno = EPROTO;
        return -1;
    }

    if (pty_contract_ioctl(slave_fd, TIOCSCTTY, 0) != 0) {
        close_if_open(master_fd);
        close_if_open(slave_fd);
        return -1;
    }

    tty_sid = tcgetsid(slave_fd);
    if (tty_sid != (__kernel_pid_t)getsid_impl(0)) {
        close_if_open(master_fd);
        close_if_open(slave_fd);
        errno = EPROTO;
        return -1;
    }

    tty_fd = open_impl("/dev/tty", O_RDWR, 0);
    if (tty_fd < 0) {
        close_if_open(master_fd);
        close_if_open(slave_fd);
        return -1;
    }

    if (tcgetsid(tty_fd) != tty_sid) {
        close_if_open(tty_fd);
        close_if_open(master_fd);
        close_if_open(slave_fd);
        errno = EPROTO;
        return -1;
    }

    close_if_open(tty_fd);
    close_if_open(master_fd);
    close_if_open(slave_fd);
    return 0;
}

int pty_session_contract_controlling_tty_survives_dup_of_slave_descriptor(void) {
    int master_fd = -1;
    int slave_fd = -1;
    int dup_fd = -1;
    int tty_fd = -1;
    unsigned int pty_index = 0;
    char tty_target[64];
    char expected_target[64];

    if (detach_controlling_tty_if_present() != 0) {
        return -1;
    }

    if (alloc_pty_pair(0, 0, &master_fd, &slave_fd, &pty_index) != 0) {
        return -1;
    }
    if (pty_contract_ioctl(slave_fd, TIOCSCTTY, 0) != 0) {
        close_if_open(master_fd);
        close_if_open(slave_fd);
        return -1;
    }

    dup_fd = dup_impl(slave_fd);
    if (dup_fd < 0) {
        close_if_open(master_fd);
        close_if_open(slave_fd);
        return -1;
    }

    close_if_open(slave_fd);
    tty_fd = open_impl("/dev/tty", O_RDWR, 0);
    if (tty_fd < 0) {
        close_if_open(dup_fd);
        close_if_open(master_fd);
        return -1;
    }

    if (readlink_fd_target(tty_fd, tty_target, sizeof(tty_target)) != 0 ||
        build_pts_path(expected_target, sizeof(expected_target), pty_index) != 0 ||
        strcmp(tty_target, expected_target) != 0) {
        close_if_open(tty_fd);
        close_if_open(dup_fd);
        close_if_open(master_fd);
        errno = EPROTO;
        return -1;
    }

    close_if_open(tty_fd);
    close_if_open(dup_fd);
    close_if_open(master_fd);
    return 0;
}

int pty_session_contract_controlling_tty_clears_or_fails_predictably_after_close_policy(void) {
    int master_fd = -1;
    int slave_fd = -1;
    int tty_fd = -1;
    unsigned int pty_index = 0;

    if (detach_controlling_tty_if_present() != 0) {
        return -1;
    }

    if (alloc_pty_pair(0, 0, &master_fd, &slave_fd, &pty_index) != 0) {
        return -1;
    }
    if (pty_contract_ioctl(slave_fd, TIOCSCTTY, 0) != 0) {
        close_if_open(master_fd);
        close_if_open(slave_fd);
        return -1;
    }

    close_if_open(slave_fd);
    tty_fd = open_impl("/dev/tty", O_RDWR, 0);
    if (tty_fd >= 0) {
        close_if_open(tty_fd);
        close_if_open(master_fd);
        errno = EPROTO;
        return -1;
    }
    if (errno != EIO && errno != ENXIO) {
        close_if_open(master_fd);
        errno = EPROTO;
        return -1;
    }

    close_if_open(master_fd);
    return 0;
}

int pty_session_contract_pty_fdinfo_reflects_flags_and_paths(void) {
    int master_fd = -1;
    int slave_fd = -1;
    unsigned int pty_index = 0;
    unsigned int flags = 0;
    char target[64];
    char expected_target[64];

    if (alloc_pty_pair(1, 1, &master_fd, &slave_fd, &pty_index) != 0) {
        return -1;
    }

    if (read_fdinfo_flags(master_fd, &flags) != 0) {
        close_if_open(master_fd);
        close_if_open(slave_fd);
        return -1;
    }
    if ((flags & O_NONBLOCK) == 0 || (flags & O_CLOEXEC) == 0) {
        close_if_open(master_fd);
        close_if_open(slave_fd);
        errno = EPROTO;
        return -1;
    }

    if (readlink_fd_target(master_fd, target, sizeof(target)) != 0 ||
        strcmp(target, "/dev/ptmx") != 0) {
        close_if_open(master_fd);
        close_if_open(slave_fd);
        errno = EPROTO;
        return -1;
    }

    if (readlink_fd_target(slave_fd, target, sizeof(target)) != 0 ||
        build_pts_path(expected_target, sizeof(expected_target), pty_index) != 0 ||
        strcmp(target, expected_target) != 0) {
        close_if_open(master_fd);
        close_if_open(slave_fd);
        errno = EPROTO;
        return -1;
    }

    close_if_open(master_fd);
    close_if_open(slave_fd);
    return 0;
}

int pty_session_contract_session_leader_exit_hangs_up_foreground_pgrp(void) {
    struct task_struct *saved = get_current();
    struct task_struct *session = NULL;
    struct task_struct *foreground = NULL;
    struct task_struct *foreground_parent = NULL;
    int master_fd = -1;
    int slave_fd = -1;
    unsigned int pty_index = 0;
    int32_t foreground_pgrp;
    int ret = -1;

    if (!saved) {
        errno = ESRCH;
        return -1;
    }

    session = task_create_child_impl(saved);
    if (!session) {
        return -1;
    }

    set_current(session);
    if (setsid_impl() != session->pid) {
        goto out;
    }

    if (alloc_pty_pair(0, 0, &master_fd, &slave_fd, &pty_index) != 0) {
        goto out;
    }
    if (pty_contract_ioctl(slave_fd, TIOCSCTTY, 0) != 0) {
        goto out;
    }

    foreground = task_create_child_impl(session);
    if (!foreground) {
        goto out;
    }
    if (setpgid_impl(foreground->pid, foreground->pid) != 0) {
        goto out;
    }
    foreground_pgrp = foreground->pid;
    if (pty_contract_ioctl(slave_fd, TIOCSPGRP, &foreground_pgrp) != 0) {
        goto out;
    }

    clear_pending_signal(foreground, SIGHUP);
    clear_pending_signal(foreground, SIGCONT);
    exit_impl(0);
    set_current(saved);

    if (!signal_is_pending(foreground, SIGHUP) ||
        !signal_is_pending(foreground, SIGCONT) ||
        !atomic_load(&foreground->signaled) ||
        atomic_load(&foreground->termsig) != SIGHUP) {
        errno = ENOMSG;
        goto out;
    }

    kernel_mutex_lock(&foreground->lock);
    if (foreground->tty != NULL) {
        kernel_mutex_unlock(&foreground->lock);
        errno = ENOTRECOVERABLE;
        goto out;
    }
    kernel_mutex_unlock(&foreground->lock);

    ret = 0;

out:
    if (get_current() != saved) {
        set_current(saved);
    }
    close_if_open(slave_fd);
    close_if_open(master_fd);
    if (foreground) {
        foreground_parent = foreground->parent;
        task_unlink_child_impl(foreground_parent, foreground);
        free_task(foreground);
    }
    if (session) {
        task_unlink_child_impl(saved, session);
        free_task(session);
    }
    return ret;
}

int pty_session_contract_session_leader_tiocnotty_hangs_up_foreground_pgrp(void) {
    struct task_struct *saved = get_current();
    struct task_struct *session = NULL;
    struct task_struct *foreground = NULL;
    struct task_struct *foreground_parent = NULL;
    int master_fd = -1;
    int slave_fd = -1;
    int tty_fd = -1;
    unsigned int pty_index = 0;
    int32_t foreground_pgrp;
    int ret = -1;

    if (!saved) {
        errno = ESRCH;
        return -1;
    }

    session = task_create_child_impl(saved);
    if (!session) {
        return -1;
    }

    set_current(session);
    if (setsid_impl() != session->pid) {
        goto out;
    }
    if (alloc_pty_pair(0, 0, &master_fd, &slave_fd, &pty_index) != 0 ||
        pty_contract_ioctl(slave_fd, TIOCSCTTY, 0) != 0) {
        goto out;
    }

    foreground = task_create_child_impl(session);
    if (!foreground || setpgid_impl(foreground->pid, foreground->pid) != 0) {
        goto out;
    }
    foreground_pgrp = foreground->pid;
    if (pty_contract_ioctl(slave_fd, TIOCSPGRP, &foreground_pgrp) != 0) {
        goto out;
    }

    tty_fd = open_impl("/dev/tty", O_RDWR, 0);
    if (tty_fd < 0) {
        goto out;
    }

    clear_pending_signal(foreground, SIGHUP);
    clear_pending_signal(foreground, SIGCONT);
    if (pty_contract_ioctl(tty_fd, TIOCNOTTY, 0) != 0) {
        goto out;
    }

    if (!signal_is_pending(foreground, SIGHUP) ||
        !signal_is_pending(foreground, SIGCONT) ||
        !atomic_load(&foreground->signaled) ||
        atomic_load(&foreground->termsig) != SIGHUP) {
        errno = ENOMSG;
        goto out;
    }

    kernel_mutex_lock(&foreground->lock);
    if (foreground->tty != NULL) {
        kernel_mutex_unlock(&foreground->lock);
        errno = ENOTRECOVERABLE;
        goto out;
    }
    kernel_mutex_unlock(&foreground->lock);

    ret = 0;

out:
    if (get_current() != saved) {
        set_current(saved);
    }
    close_if_open(tty_fd);
    close_if_open(slave_fd);
    close_if_open(master_fd);
    if (foreground) {
        foreground_parent = foreground->parent;
        task_unlink_child_impl(foreground_parent, foreground);
        free_task(foreground);
    }
    if (session) {
        task_unlink_child_impl(saved, session);
        free_task(session);
    }
    return ret;
}

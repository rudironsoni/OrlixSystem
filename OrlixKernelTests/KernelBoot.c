/* OrlixKernelTests/KernelBoot.c
 * C translation unit for Linux virtual kernel boot contract.
 *
 * Proves the virtual kernel reaches a deterministic ready state.
 * Compiled in a Linux-UAPI-clean context.
 * Uses canonical Linux names directly.
 */

#include <errno.h>
#include <string.h>

#include <linux/fcntl.h>

#include "fs/vfs.h"
#include "fs/fdtable.h"
#include "kernel/task.h"
#include "kernel/init.h"

extern int open_impl(const char *pathname, int flags, uint32_t mode);
extern int close_impl(int fd);
extern long read_impl(int fd, void *buf, size_t count);
extern long readlink_impl(const char *pathname, char *buf, size_t bufsiz);

static int path_has_orlix_suffix(const char *path) {
    size_t len;
    static const char suffix[] = "/Orlix";
    size_t suffix_len = sizeof(suffix) - 1;

    if (!path) {
        return 0;
    }
    len = strlen(path);
    if (len < suffix_len) {
        return 0;
    }
    return strcmp(path + (len - suffix_len), suffix) == 0;
}

static int path_contains_legacy_leaf(const char *path) {
    static const char legacy_leaf[] = {
        '/', 'I', 'X', 'L', 'a', 'n', 'd', '\0'
    };

    return path && strstr(path, legacy_leaf) != NULL;
}

/* Test 1: System is booted */
int kernel_boot_test_system_booted(void) {
    if (!kernel_is_booted()) {
        return -1;
    }
    return 0;
}

/* Test 2: VFS backing roots are discoverable and non-empty */
int kernel_boot_test_vfs_backing_roots(void) {
    const char *persistent;
    const char *cache;
    const char *temp;

    persistent = vfs_persistent_backing_root();
    cache = vfs_cache_backing_root();
    temp = vfs_temp_backing_root();

    if (!persistent || persistent[0] == '\0') {
        return -1;
    }
    if (!cache || cache[0] == '\0') {
        return -2;
    }
    if (!temp || temp[0] == '\0') {
        return -3;
    }

    /* Persistent root must not be temp-backed */
    if (strcmp(persistent, temp) == 0) {
        return -4;
    }
    if (strstr(persistent, "/Application Support/") == NULL) {
        return -5;
    }
    if (!path_has_orlix_suffix(persistent)) {
        return -6;
    }
    if (strstr(cache, "/Caches/") == NULL) {
        return -7;
    }
    if (!path_has_orlix_suffix(cache)) {
        return -8;
    }
    if (path_contains_legacy_leaf(persistent) || path_contains_legacy_leaf(cache)) {
        return -9;
    }
    if (path_contains_legacy_leaf(temp)) {
        return -10;
    }

    return 0;
}

/* Test 3: VFS route table routes correctly */
int kernel_boot_test_vfs_routing(void) {
    if (vfs_backing_class_for_path("/") != VFS_BACKING_PERSISTENT) {
        return -1;
    }
    if (vfs_backing_class_for_path("/tmp") != VFS_BACKING_TEMP) {
        return -2;
    }
    if (vfs_backing_class_for_path("/var/cache") != VFS_BACKING_CACHE) {
        return -3;
    }
    return 0;
}

/* Test 4: Synthetic roots are available */
int kernel_boot_test_synthetic_roots(void) {
    struct linux_stat st;

    /* /proc is synthetic */
    if (!vfs_path_is_synthetic("/proc")) {
        return -1;
    }
    /* /dev is synthetic */
    if (!vfs_path_is_synthetic("/dev")) {
        return -2;
    }

    /* Synthetic root stat succeeds */
    if (vfs_fstatat(AT_FDCWD, "/proc", &st, 0) != 0) {
        return -3;
    }
    if (vfs_fstatat(AT_FDCWD, "/dev", &st, 0) != 0) {
        return -4;
    }

    /* /sys is synthetic if modeled */
    if (vfs_path_is_synthetic("/sys")) {
        if (vfs_fstatat(AT_FDCWD, "/sys", &st, 0) != 0) {
            return -5;
        }
    }

    return 0;
}

/* Test 5: Task system is initialized */
int kernel_boot_test_task_init(void) {
    struct task_struct *init;

    init = init_task;
    if (!init) {
        return -1;
    }
    if (init->pid <= 0) {
        return -2;
    }
    if (!init->fs) {
        return -3;
    }
    if (init->fs->root_path[0] == '\0') {
        return -4;
    }
    if (init->fs->pwd_path[0] == '\0') {
        return -5;
    }
    return 0;
}

int kernel_boot_test_init_identity_is_pid_namespace_root(void) {
    struct task_struct *init = init_task;

    if (!init) {
        return -1;
    }
    if (init->pid != 1 || init->tgid != 1 || init->ppid != 0) {
        return -2;
    }
    if (init->pgid != 1 || init->sid != 1) {
        return -3;
    }
    if (init->ns_pid != 1 || init->pid_ns_level != 0) {
        return -4;
    }
    if (strcmp(init->comm, "init") != 0) {
        return -5;
    }
    if (init->exe[0] != '\0') {
        return -6;
    }
    return 0;
}

static int kernel_boot_parse_proc_stat_starttime(const char *buf,
                                                 unsigned long long *starttime_out) {
    const char *cursor;
    int field = 3;

    if (!buf || !starttime_out) {
        errno = EINVAL;
        return -1;
    }

    cursor = strrchr(buf, ')');
    if (!cursor || cursor[1] != ' ') {
        errno = EPROTO;
        return -1;
    }
    cursor += 2;

    while (*cursor != '\0' && field < 22) {
        while (*cursor == ' ') {
            cursor++;
        }
        while (*cursor != '\0' && *cursor != ' ') {
            cursor++;
        }
        field++;
    }
    while (*cursor == ' ') {
        cursor++;
    }
    if (*cursor < '0' || *cursor > '9') {
        errno = EPROTO;
        return -1;
    }

    *starttime_out = 0;
    while (*cursor >= '0' && *cursor <= '9') {
        *starttime_out = (*starttime_out * 10ULL) + (unsigned long long)(*cursor - '0');
        cursor++;
    }
    return 0;
}

int kernel_boot_test_task_start_time_is_kernel_owned(void) {
    struct task_struct *init = init_task;
    struct task_struct *child = NULL;
    int ret = -1;

    if (!init) {
        return -1;
    }
    if (init->start_time_ns == 0) {
        return -2;
    }

    child = task_create_child_impl(init);
    if (!child) {
        return -3;
    }
    if (child->start_time_ns == 0 || child->start_time_ns < init->start_time_ns) {
        ret = -4;
        goto out;
    }
    ret = 0;

out:
    task_unlink_child_impl(init, child);
    free_task(child);
    return ret;
}

int kernel_boot_test_proc_self_stat_reports_start_time(void) {
    int fd = -1;
    char buf[512];
    long nread;
    unsigned long long starttime = 0;

    fd = open_impl("/proc/self/stat", O_RDONLY, 0);
    if (fd < 0) {
        return -1;
    }
    nread = read_impl(fd, buf, sizeof(buf) - 1);
    close_impl(fd);
    if (nread <= 0) {
        return -2;
    }
    buf[nread] = '\0';
    if (kernel_boot_parse_proc_stat_starttime(buf, &starttime) != 0) {
        return -3;
    }
    if (starttime == 0) {
        return -4;
    }
    return 0;
}

int kernel_boot_test_proc_self_tree_is_available(void) {
    int fd;

    fd = open_impl("/proc/self", O_RDONLY | O_DIRECTORY, 0);
    if (fd < 0) {
        return -1;
    }
    close_impl(fd);

    fd = open_impl("/proc/self/fd", O_RDONLY | O_DIRECTORY, 0);
    if (fd < 0) {
        return -2;
    }
    close_impl(fd);

    fd = open_impl("/proc/self/fdinfo", O_RDONLY | O_DIRECTORY, 0);
    if (fd < 0) {
        return -3;
    }
    close_impl(fd);

    fd = open_impl("/proc/self/ns", O_RDONLY | O_DIRECTORY, 0);
    if (fd < 0) {
        return -4;
    }
    close_impl(fd);
    return 0;
}

int kernel_boot_test_stdio_fd_links_are_virtual_dev_null(void) {
    static const char *const paths[] = {
        "/proc/self/fd/0",
        "/proc/self/fd/1",
        "/proc/self/fd/2",
    };
    char buf[64];
    long len;

    for (int fd = 0; fd < 3; fd++) {
        len = readlink_impl(paths[fd], buf, sizeof(buf) - 1);
        if (len < 0) {
            return -2;
        }
        buf[len] = '\0';
        if (strcmp(buf, "/dev/null") != 0) {
            return -3;
        }
    }
    return 0;
}

/* Test 6: FD table is ready */
int kernel_boot_test_fd_table(void) {
    int fd;

    /* stdio fds should be allocated */
    if (!fdtable_is_used_impl(0) || !fdtable_is_used_impl(1) || !fdtable_is_used_impl(2)) {
        return -1;
    }

    /* Allocate a new fd - should not collide with stdio */
    fd = alloc_fd_impl();
    if (fd < 0) {
        return -2;
    }
    if (fd < 3) {
        free_fd_impl(fd);
        return -3;
    }

    free_fd_impl(fd);
    return 0;
}

/* Test 7: Boot is idempotent */
int kernel_boot_test_idempotent(void) {
    int first;
    int second;

    first = start_kernel();
    second = start_kernel();

    if (first != 0) {
        return -1;
    }
    if (second != 0) {
        return -2;
    }
    return 0;
}

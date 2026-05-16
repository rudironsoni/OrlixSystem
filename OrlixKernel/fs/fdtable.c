#include "fdtable.h"

#include <linux/close_range.h>
#include <linux/errno.h>
#include <linux/fcntl.h>
#include <linux/ioctl.h>
/*
 * These upstream Linux ABI headers recurse through <linux/fcntl.h>. Keep them on the upstream Linux ABI
 * contract path in this translation unit instead of pulling the full kernel
 * owner graph.
 */
#define _LINUX_FCNTL_H
#include <linux/eventfd.h>
#include <linux/memfd.h>
#include <linux/pidfd.h>
#include <linux/timerfd.h>
#undef _LINUX_FCNTL_H
#include <linux/limits.h>
#include <linux/string.h>
#include <linux/stat.h>
#include <linux/time.h>
#include <asm/stat.h>

#include "internal/slab.h"
#include "internal/timekeeping.h"
#include "private/fs/fdtable_state.h"
#include "../private/kernel/task_state.h"
#include "private/fs/lock_state.h"

/* Standard file descriptors - local definitions to avoid Darwin <unistd.h> */
#ifndef STDIN_FILENO
#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2
#endif
#include "internal/fs/file.h"
#include "internal/fs/memfd.h"
#include "eventpoll.h"
#include "private/fs/eventpoll_state.h"
#include "private/fs/readiness_state.h"
#include "private/fs/pipe_state.h"
#include "pipe.h"
#include "private/fs/pty_state.h"
#include "vfs.h"
#include "../private/kernel/net/endpoint_state.h"
#include "../kernel/task.h"

static void retain_fd_description(struct fd_description *desc);
static void release_fd_description(struct fd_description *desc);
static struct fd_file *copy_file_descriptor(struct fd_file *file);
extern int scnprintf(char *buf, size_t size, const char *fmt, ...);

struct fd_table *alloc_files(size_t max_fds) {
    if (max_fds == 0) {
        return NULL;
    }

    struct fd_table *files = __kmalloc_noprof(sizeof(struct fd_table), GFP_KERNEL | __GFP_ZERO);
    if (!files) {
        return NULL;
    }

    files->fd = __kmalloc_noprof(max_fds * sizeof(struct fd_file *), GFP_KERNEL | __GFP_ZERO);
    if (!files->fd) {
        kfree(files);
        return NULL;
    }

    files->max_fds = max_fds;
    atomic_set(&files->refs, 1);
    fs_mutex_init(&files->lock);

    return files;
}

void free_files(struct fd_table *files) {
    if (!files)
        return;
    if (atomic_dec_return(&files->refs) != 0) {
        return;
    }

    fs_mutex_lock(&files->lock);
    for (size_t i = 0; i < files->max_fds; i++) {
        if (files->fd[i]) {
            free_file(files->fd[i]);
        }
    }
    fs_mutex_unlock(&files->lock);

    kfree(files->fd);
    fs_mutex_destroy(&files->lock);
    kfree(files);
}

struct fd_table *get_files(struct fd_table *files) {
    if (files) {
        atomic_inc(&files->refs);
    }
    return files;
}

struct fd_table *dup_files(struct fd_table *parent) {
    if (!parent) {
        return NULL;
    }

    struct fd_table *child = alloc_files(parent->max_fds);
    if (!child)
        return NULL;

    fs_mutex_lock(&parent->lock);
    for (size_t i = 0; i < parent->max_fds; i++) {
        if (parent->fd[i]) {
            child->fd[i] = copy_file_descriptor(parent->fd[i]);
            if (!child->fd[i]) {
                fs_mutex_unlock(&parent->lock);
                free_files(child);
                return NULL;
            }
        }
    }
    fs_mutex_unlock(&parent->lock);

    return child;
}

struct fd_file *alloc_file(void) {
    struct fd_file *file = __kmalloc_noprof(sizeof(struct fd_file), GFP_KERNEL | __GFP_ZERO);
    if (file) {
        atomic_set(&file->refs, 1);
    }
    return file;
}

void free_file(struct fd_file *file) {
    if (!file)
        return;
    if (atomic_dec_return(&file->refs) == 0) {
        release_fd_description((fd_description_t *)file->private_data);
        kfree(file);
    }
}

struct fd_file *dup_file(struct fd_file *file) {
    if (!file)
        return NULL;
    atomic_inc(&file->refs);
    return file;
}

static struct fd_file *copy_file_descriptor(struct fd_file *file) {
    struct fd_file *copy;

    if (!file) {
        return NULL;
    }

    copy = alloc_file();
    if (!copy) {
        return NULL;
    }

    copy->fd = file->fd;
    copy->real_fd = file->real_fd;
    copy->flags = file->flags;
    copy->fd_flags = file->fd_flags;
    copy->pos = file->pos;
    memcpy(copy->path, file->path, sizeof(copy->path));
    copy->private_data = file->private_data;
    retain_fd_description((fd_description_t *)copy->private_data);
    return copy;
}

int alloc_fd(struct fd_table *files, struct fd_file *file) {
    if (!files || !file) {
        return -EINVAL;
    }

    fs_mutex_lock(&files->lock);
    for (size_t i = 0; i < files->max_fds; i++) {
        if (!files->fd[i]) {
            files->fd[i] = file;
            fs_mutex_unlock(&files->lock);
            return (int)i;
        }
    }
    fs_mutex_unlock(&files->lock);

    return -EMFILE;
}

int free_fd(struct fd_table *files, int fd) {
    if (!files || fd < 0 || (size_t)fd >= files->max_fds) {
        return -EBADF;
    }

    fs_mutex_lock(&files->lock);
    struct fd_file *file = files->fd[fd];
    if (!file) {
        fs_mutex_unlock(&files->lock);
        return -EBADF;
    }

    files->fd[fd] = NULL;
    free_file(file);
    fs_mutex_unlock(&files->lock);

    return 0;
}

struct fd_file *fget(struct fd_table *files, int fd) {
    if (!files || fd < 0 || (size_t)fd >= files->max_fds) {
        return NULL;
    }

    fs_mutex_lock(&files->lock);
    struct fd_file *file = files->fd[fd];
    fs_mutex_unlock(&files->lock);

    return file;
}

int dup_fd(struct fd_table *files, int oldfd) {
    if (!files || oldfd < 0 || (size_t)oldfd >= files->max_fds) {
        return -EBADF;
    }

    fs_mutex_lock(&files->lock);
    struct fd_file *file = files->fd[oldfd];
    if (!file) {
        fs_mutex_unlock(&files->lock);
        return -EBADF;
    }

    for (size_t i = 0; i < files->max_fds; i++) {
        if (!files->fd[i]) {
            files->fd[i] = file;
            atomic_inc(&file->refs);
            fs_mutex_unlock(&files->lock);
            return (int)i;
        }
    }
    fs_mutex_unlock(&files->lock);

    return -EMFILE;
}

int do_dup2(struct fd_table *files, int oldfd, int newfd) {
    if (!files || oldfd < 0 || newfd < 0 || (size_t)oldfd >= files->max_fds ||
        (size_t)newfd >= files->max_fds) {
        return -EBADF;
    }

    // dup2 to same FD is a no-op
    if (oldfd == newfd) {
        return 0;
    }

    fs_mutex_lock(&files->lock);
    struct fd_file *file = files->fd[oldfd];
    if (!file) {
        fs_mutex_unlock(&files->lock);
        return -EBADF;
    }

    if (files->fd[newfd]) {
        free_file(files->fd[newfd]);
    }

    files->fd[newfd] = file;
    atomic_inc(&file->refs);
    fs_mutex_unlock(&files->lock);

    return 0;
}

int set_cloexec(struct fd_table *files, int fd, bool cloexec) {
    if (!files || fd < 0 || (size_t)fd >= files->max_fds) {
        return -EBADF;
    }

    fs_mutex_lock(&files->lock);
    struct fd_file *file = files->fd[fd];
    if (!file) {
        fs_mutex_unlock(&files->lock);
        return -EBADF;
    }

    if (cloexec) {
        file->fd_flags |= FD_CLOEXEC;
    } else {
        file->fd_flags &= ~FD_CLOEXEC;
    }
    fs_mutex_unlock(&files->lock);

    return 0;
}

bool get_cloexec(struct fd_table *files, int fd) {
    if (!files || fd < 0 || (size_t)fd >= files->max_fds) {
        return false;
    }

    fs_mutex_lock(&files->lock);
    struct fd_file *file = files->fd[fd];
    bool cloexec = false;
    if (file) {
        cloexec = (file->fd_flags & FD_CLOEXEC) != 0;
    }
    fs_mutex_unlock(&files->lock);

    return cloexec;
}

int close_on_exec(struct fd_table *files) {
    if (!files) {
        return -EINVAL;
    }

    int closed = 0;
    fs_mutex_lock(&files->lock);
    for (size_t i = 0; i < files->max_fds; i++) {
        if (files->fd[i] && (files->fd[i]->fd_flags & FD_CLOEXEC)) {
            free_file(files->fd[i]);
            files->fd[i] = NULL;
            closed++;
        }
    }
    fs_mutex_unlock(&files->lock);

    return closed;
}

/* ============================================================================
 * SINGLE STATIC FD TABLE (for host-mediated FDs)
 * This is the internal implementation - external code should use the API above
 * ============================================================================ */

enum fd_type {
    FD_TYPE_HOST, /* Normal host-backed fd */
    FD_TYPE_SYNTHETIC_DIR, /* Synthetic directory (no host backing) */
    FD_TYPE_SYNTHETIC_DEV, /* Synthetic char device (no host backing) */
    FD_TYPE_SYNTHETIC_PROC_FILE, /* Synthetic proc file (no host backing) */
    FD_TYPE_SYNTHETIC_PTY, /* Synthetic PTY endpoint */
    FD_TYPE_PIPE, /* Virtual pipe endpoint */
    FD_TYPE_SOCKET, /* Virtual socket endpoint */
    FD_TYPE_EPOLL, /* Virtual epoll instance */
    FD_TYPE_MOUNT, /* Virtual detached mount tree */
    FD_TYPE_EVENTFD, /* Virtual event counter */
    FD_TYPE_TIMERFD, /* Virtual timer expiration counter */
    FD_TYPE_MEMFD, /* Anonymous regular file */
    FD_TYPE_PIDFD /* Virtual pidfd task handle */
};

typedef struct synthetic_dir_state {
    __kernel_off_t cursor;
    bool entries_emitted;
    synthetic_dir_class_t dir_class;
} synthetic_dir_state_t;

typedef struct fd_description {
    enum fd_type type;
    int fd;
    int flags;
    uint32_t mode;
    __kernel_off_t offset;
    char path[MAX_PATH];
    uint64_t file_identity;
    uint64_t mount_ns_id;
    bool path_deleted;
    bool is_dir;
    void *synthetic_state;
    synthetic_dev_node_t dev_node;
    synthetic_proc_file_t proc_file;
    int proc_file_fd_num;
    int proc_file_target_pid;
    char cgroupfs_path[MAX_PATH];
    int cgroupfs_node;
    unsigned int pty_index;
    bool pty_is_master;
    struct pipe_endpoint *pipe_endpoint;
    struct socket_state *socket;
    struct epoll_instance *epoll_instance;
    struct vfs_mount_fd mount_fd;
    uint64_t eventfd_counter;
    bool eventfd_semaphore;
    int timerfd_clockid;
    uint64_t timerfd_interval_ns;
    uint64_t timerfd_next_ns;
    uint64_t timerfd_expirations;
    bool timerfd_armed;
    int memfd_seals;
    struct task *pidfd_task;
    atomic_t refs;
    fs_mutex_t lock;
} fd_description_t;



static fd_entry_t fd_table[NR_OPEN_DEFAULT];
static fs_mutex_t fd_table_lock = FS_MUTEX_INITIALIZER;
static atomic_t fd_table_initialized = ATOMIC_INIT(0);
static atomic64_t fdtable_next_virtual_identity = ATOMIC64_INIT(1);
static __thread fd_entry_t fd_task_local_entry;

static bool fdtable_task_has_file(struct task *task, int fd) {
    bool used;

    if (!task || !task->files || fd < 0 || (size_t)fd >= task->files->max_fds) {
        return false;
    }

    fs_mutex_lock(&task->files->lock);
    used = task->files->fd[fd] != NULL;
    fs_mutex_unlock(&task->files->lock);
    return used;
}

static uint64_t fdtable_current_mount_namespace_id(void) {
    struct task *task = task_current();

    if (!task || !task->fs) {
        return 0;
    }
    return fs_mount_namespace_id(task->fs);
}

static bool fdtable_any_task_uses_fd(int fd) {
    bool used = false;

    if (fd < 0) {
        return false;
    }

    kernel_mutex_lock(&task_table_lock);
    for (int i = 0; i < TASK_MAX_TASKS && !used; i++) {
        struct task *task = task_table[i];
        while (task) {
            if (fdtable_task_has_file(task, fd)) {
                used = true;
                break;
            }
            task = task->hash_next;
        }
    }
    kernel_mutex_unlock(&task_table_lock);
    return used;
}

static void fdtable_update_file_offsets_for_desc(fd_description_t *desc, int64_t offset) {
    if (!desc) {
        return;
    }

    kernel_mutex_lock(&task_table_lock);
    for (int i = 0; i < TASK_MAX_TASKS; i++) {
        struct task *task = task_table[i];
        while (task) {
            if (task->files) {
                fs_mutex_lock(&task->files->lock);
                for (size_t fd = 0; fd < task->files->max_fds; fd++) {
                    struct fd_file *file = task->files->fd[fd];
                    if (file && (fd_description_t *)file->private_data == desc) {
                        file->pos = offset;
                    }
                }
                fs_mutex_unlock(&task->files->lock);
            }
            task = task->hash_next;
        }
    }
    kernel_mutex_unlock(&task_table_lock);
}

static void fdtable_mark_desc_deleted_if_path_matches(fd_description_t *desc, const char *path) {
    if (!desc || !path || desc->path[0] == '\0') {
        return;
    }
    fs_mutex_lock(&desc->lock);
    if (strcmp(desc->path, path) == 0) {
        desc->path_deleted = true;
    }
    fs_mutex_unlock(&desc->lock);
}

static void fdtable_rename_desc_path_if_matches(fd_description_t *desc,
                                                const char *old_path,
                                                const char *new_path) {
    if (!desc || !old_path || !new_path || desc->path[0] == '\0') {
        return;
    }
    fs_mutex_lock(&desc->lock);
    if (strcmp(desc->path, old_path) == 0) {
        strncpy(desc->path, new_path, sizeof(desc->path) - 1);
        desc->path[sizeof(desc->path) - 1] = '\0';
        desc->path_deleted = false;
    }
    fs_mutex_unlock(&desc->lock);
}

static void fdtable_exchange_desc_path_if_matches(fd_description_t *desc,
                                                  const char *left_path,
                                                  const char *right_path) {
    if (!desc || !left_path || !right_path || desc->path[0] == '\0') {
        return;
    }
    fs_mutex_lock(&desc->lock);
    if (strcmp(desc->path, left_path) == 0) {
        strncpy(desc->path, right_path, sizeof(desc->path) - 1);
        desc->path[sizeof(desc->path) - 1] = '\0';
        desc->path_deleted = false;
    } else if (strcmp(desc->path, right_path) == 0) {
        strncpy(desc->path, left_path, sizeof(desc->path) - 1);
        desc->path[sizeof(desc->path) - 1] = '\0';
        desc->path_deleted = false;
    }
    fs_mutex_unlock(&desc->lock);
}

static void fdtable_sync_task_file_locked(int fd, fd_entry_t *entry) {
    struct task *task = task_current();
    struct fd_file *file;
    fd_description_t *new_desc = entry ? entry->desc : NULL;

    if (!task || !task->files || fd < 0 || (size_t)fd >= task->files->max_fds) {
        return;
    }

    fs_mutex_lock(&task->files->lock);
    file = task->files->fd[fd];
    if (!file) {
        file = alloc_file();
        if (!file) {
            fs_mutex_unlock(&task->files->lock);
            return;
        }
        task->files->fd[fd] = file;
    }
    if ((fd_description_t *)file->private_data != new_desc) {
        release_fd_description((fd_description_t *)file->private_data);
        retain_fd_description(new_desc);
        file->private_data = new_desc;
    }
    file->fd = fd;
    file->real_fd = new_desc ? new_desc->fd : -1;
    file->flags = new_desc ? (unsigned int)new_desc->flags : 0;
    file->fd_flags = entry ? (unsigned int)entry->fd_flags : 0;
        file->pos = new_desc ? (int64_t)new_desc->offset : 0;
    if (new_desc) {
        strncpy(file->path, new_desc->path, sizeof(file->path) - 1);
        file->path[sizeof(file->path) - 1] = '\0';
    } else {
        file->path[0] = '\0';
    }
    fs_mutex_unlock(&task->files->lock);
}

static void fdtable_remove_task_file(int fd) {
    struct task *task = task_current();
    struct fd_file *file;

    if (!task || !task->files || fd < 0 || (size_t)fd >= task->files->max_fds) {
        return;
    }

    fs_mutex_lock(&task->files->lock);
    file = task->files->fd[fd];
    task->files->fd[fd] = NULL;
    fs_mutex_unlock(&task->files->lock);
    if (file) {
        free_file(file);
    }
}

static fd_description_t *alloc_fd_description(int real_fd, int flags, uint32_t mode, const char *path) {
    fd_description_t *desc = __kmalloc_noprof(sizeof(fd_description_t), GFP_KERNEL | __GFP_ZERO);
    if (!desc) {
        return NULL;
    }

    desc->type = FD_TYPE_HOST;
    desc->fd = real_fd;
    desc->flags = flags;
    desc->mode = mode;
    desc->offset = 0;
    desc->file_identity = 0;
    desc->mount_ns_id = fdtable_current_mount_namespace_id();
    desc->is_dir = (flags & O_DIRECTORY) != 0;
    desc->synthetic_state = NULL;
    desc->dev_node = SYNTHETIC_DEV_NONE;
    desc->proc_file = SYNTHETIC_PROC_FILE_NONE;
    desc->proc_file_fd_num = -1;
    desc->proc_file_target_pid = -1;
    desc->pty_index = 0;
    desc->pty_is_master = false;
    desc->pipe_endpoint = NULL;
    memset(&desc->mount_fd, 0, sizeof(desc->mount_fd));
    desc->synthetic_state = __kmalloc_noprof(sizeof(synthetic_dir_state_t), GFP_KERNEL | __GFP_ZERO);

    if (!desc->synthetic_state) {
        kfree(desc);
        return NULL;
    }
    ((synthetic_dir_state_t *)desc->synthetic_state)->dir_class = SYNTHETIC_DIR_GENERIC;
    atomic_set(&desc->refs, 1);
    fs_mutex_init(&desc->lock);
    if (path) {
        strncpy(desc->path, path, MAX_PATH - 1);
        desc->path[MAX_PATH - 1] = '\0';
    }
    return desc;
}

static fd_description_t *alloc_fd_description_with_identity(int real_fd, int flags, uint32_t mode,
                                                            const char *path,
                                                            uint64_t file_identity) {
    fd_description_t *desc = alloc_fd_description(real_fd, flags, mode, path);
    if (desc) {
        desc->file_identity = file_identity;
    }
    return desc;
}

static fd_description_t *alloc_synthetic_subdir_fd_description(int flags, uint32_t mode, const char *path, synthetic_dir_class_t dir_class) {
    fd_description_t *desc = __kmalloc_noprof(sizeof(fd_description_t), GFP_KERNEL | __GFP_ZERO);
    if (!desc) {
        return NULL;
    }
    desc->type = FD_TYPE_SYNTHETIC_DIR;
    desc->fd = -1;
    desc->flags = flags;
    desc->mode = mode;
    desc->offset = 0;
    desc->is_dir = true;
    desc->dev_node = SYNTHETIC_DEV_NONE;
    desc->proc_file = SYNTHETIC_PROC_FILE_NONE;
    desc->proc_file_fd_num = -1;
    desc->proc_file_target_pid = -1;
    desc->pty_index = 0;
    desc->pty_is_master = false;
    desc->pipe_endpoint = NULL;
    memset(&desc->mount_fd, 0, sizeof(desc->mount_fd));
    desc->synthetic_state = __kmalloc_noprof(sizeof(synthetic_dir_state_t), GFP_KERNEL | __GFP_ZERO);
    if (!desc->synthetic_state) {
        kfree(desc);
        return NULL;
    }
    ((synthetic_dir_state_t *)desc->synthetic_state)->dir_class = dir_class;
    atomic_set(&desc->refs, 1);
    fs_mutex_init(&desc->lock);
    if (path) {
        strncpy(desc->path, path, MAX_PATH - 1);
        desc->path[MAX_PATH - 1] = '\0';
    }
    return desc;
}

static fd_description_t *alloc_synthetic_dev_fd_description(int flags, uint32_t mode, const char *path, synthetic_dev_node_t dev_node) {
    fd_description_t *desc = __kmalloc_noprof(sizeof(fd_description_t), GFP_KERNEL | __GFP_ZERO);
    if (!desc) {
        return NULL;
    }
    desc->type = FD_TYPE_SYNTHETIC_DEV;
    desc->fd = -1;
    desc->flags = flags;
    desc->mode = mode;
    desc->offset = 0;
    desc->is_dir = false;
    desc->dev_node = dev_node;
    desc->proc_file = SYNTHETIC_PROC_FILE_NONE;
    desc->proc_file_fd_num = -1;
    desc->proc_file_target_pid = -1;
    desc->pty_index = 0;
    desc->pty_is_master = false;
    desc->pipe_endpoint = NULL;
    memset(&desc->mount_fd, 0, sizeof(desc->mount_fd));
    desc->synthetic_state = NULL;
    atomic_set(&desc->refs, 1);
    fs_mutex_init(&desc->lock);
    if (path) {
        strncpy(desc->path, path, MAX_PATH - 1);
        desc->path[MAX_PATH - 1] = '\0';
    }
    return desc;
}

static fd_description_t *alloc_synthetic_proc_file_fd_description(int flags, uint32_t mode, const char *path, synthetic_proc_file_t proc_file) {
    fd_description_t *desc = __kmalloc_noprof(sizeof(fd_description_t), GFP_KERNEL | __GFP_ZERO);
    if (!desc) {
        return NULL;
    }
    desc->type = FD_TYPE_SYNTHETIC_PROC_FILE;
    desc->fd = -1;
    desc->flags = flags;
    desc->mode = mode;
    desc->offset = 0;
    desc->is_dir = false;
    desc->dev_node = SYNTHETIC_DEV_NONE;
    desc->proc_file = proc_file;
    desc->proc_file_fd_num = -1;
    desc->proc_file_target_pid = -1;
    desc->cgroupfs_node = 0;
    desc->pty_index = 0;
    desc->pty_is_master = false;
    desc->pipe_endpoint = NULL;
    memset(&desc->mount_fd, 0, sizeof(desc->mount_fd));
    desc->synthetic_state = NULL;
    atomic_set(&desc->refs, 1);
    fs_mutex_init(&desc->lock);
    if (path) {
        strncpy(desc->path, path, MAX_PATH - 1);
        desc->path[MAX_PATH - 1] = '\0';
    }
    return desc;
}

static fd_description_t *alloc_synthetic_pty_fd_description(int flags, uint32_t mode, const char *path,
                                                             unsigned int pty_index, bool is_master) {
    fd_description_t *desc = __kmalloc_noprof(sizeof(fd_description_t), GFP_KERNEL | __GFP_ZERO);
    if (!desc) {
        return NULL;
    }
    desc->type = FD_TYPE_SYNTHETIC_PTY;
    desc->fd = -1;
    desc->flags = flags;
    desc->mode = mode;
    desc->offset = 0;
    desc->is_dir = false;
    desc->dev_node = SYNTHETIC_DEV_NONE;
    desc->proc_file = SYNTHETIC_PROC_FILE_NONE;
    desc->proc_file_fd_num = -1;
    desc->proc_file_target_pid = -1;
    desc->pty_index = pty_index;
    desc->pty_is_master = is_master;
    desc->pipe_endpoint = NULL;
    memset(&desc->mount_fd, 0, sizeof(desc->mount_fd));
    desc->synthetic_state = NULL;
    atomic_set(&desc->refs, 1);
    fs_mutex_init(&desc->lock);
    if (path) {
        strncpy(desc->path, path, MAX_PATH - 1);
        desc->path[MAX_PATH - 1] = '\0';
    }
    return desc;
}

static fd_description_t *alloc_pipe_fd_description(int flags, struct pipe_endpoint *endpoint) {
    fd_description_t *desc;
    char path[MAX_PATH];
    unsigned long long pipe_id;

    if (!endpoint) {
        return NULL;
    }

    desc = __kmalloc_noprof(sizeof(fd_description_t), GFP_KERNEL | __GFP_ZERO);
    if (!desc) {
        return NULL;
    }

    pipe_id = pipe_endpoint_id_impl(endpoint);
    scnprintf(path, sizeof(path), "pipe:[%llu]", pipe_id);

    desc->type = FD_TYPE_PIPE;
    desc->fd = -1;
    desc->flags = flags;
    desc->mode = 0;
    desc->offset = 0;
    desc->is_dir = false;
    desc->dev_node = SYNTHETIC_DEV_NONE;
    desc->proc_file = SYNTHETIC_PROC_FILE_NONE;
    desc->proc_file_fd_num = -1;
    desc->proc_file_target_pid = -1;
    desc->pty_index = 0;
    desc->pty_is_master = false;
    desc->pipe_endpoint = endpoint;
    memset(&desc->mount_fd, 0, sizeof(desc->mount_fd));
    desc->synthetic_state = NULL;
    atomic_set(&desc->refs, 1);
    fs_mutex_init(&desc->lock);
    memcpy(desc->path, path, strlen(path) + 1);
    return desc;
}

static fd_description_t *alloc_socket_fd_description(int flags, struct socket_state *socket) {
    fd_description_t *desc;
    unsigned long long socket_id;

    if (!socket) {
        return NULL;
    }

    desc = __kmalloc_noprof(sizeof(fd_description_t), GFP_KERNEL | __GFP_ZERO);
    if (!desc) {
        return NULL;
    }

    desc->type = FD_TYPE_SOCKET;
    desc->fd = -1;
    desc->flags = flags;
    desc->mode = 0;
    desc->offset = 0;
    desc->is_dir = false;
    desc->dev_node = SYNTHETIC_DEV_NONE;
    desc->proc_file = SYNTHETIC_PROC_FILE_NONE;
    desc->proc_file_fd_num = -1;
    desc->proc_file_target_pid = -1;
    desc->pty_index = 0;
    desc->pty_is_master = false;
    desc->pipe_endpoint = NULL;
    desc->socket = socket;
    desc->epoll_instance = NULL;
    memset(&desc->mount_fd, 0, sizeof(desc->mount_fd));
    desc->synthetic_state = NULL;
    atomic_set(&desc->refs, 1);
    fs_mutex_init(&desc->lock);
    socket_id = socket_identity_impl(socket);
    scnprintf(desc->path, sizeof(desc->path), "socket:[%llu]", socket_id);
    return desc;
}

static fd_description_t *alloc_epoll_fd_description(int flags, struct epoll_instance *instance) {
    fd_description_t *desc;

    if (!instance) {
        return NULL;
    }

    desc = __kmalloc_noprof(sizeof(fd_description_t), GFP_KERNEL | __GFP_ZERO);
    if (!desc) {
        return NULL;
    }

    desc->type = FD_TYPE_EPOLL;
    desc->fd = -1;
    desc->flags = flags;
    desc->mode = 0;
    desc->offset = 0;
    desc->is_dir = false;
    desc->dev_node = SYNTHETIC_DEV_NONE;
    desc->proc_file = SYNTHETIC_PROC_FILE_NONE;
    desc->proc_file_fd_num = -1;
    desc->proc_file_target_pid = -1;
    desc->pty_index = 0;
    desc->pty_is_master = false;
    desc->pipe_endpoint = NULL;
    desc->epoll_instance = instance;
    memset(&desc->mount_fd, 0, sizeof(desc->mount_fd));
    desc->synthetic_state = NULL;
    atomic_set(&desc->refs, 1);
    fs_mutex_init(&desc->lock);
    memcpy(desc->path, "anon_inode:[eventpoll]", sizeof("anon_inode:[eventpoll]"));
    return desc;
}

static fd_description_t *alloc_eventfd_description(unsigned int initval, int flags) {
    fd_description_t *desc = __kmalloc_noprof(sizeof(fd_description_t), GFP_KERNEL | __GFP_ZERO);
    if (!desc) {
        return NULL;
    }

    desc->type = FD_TYPE_EVENTFD;
    desc->fd = -1;
    desc->flags = O_RDWR | (flags & O_NONBLOCK);
    desc->mode = 0;
    desc->offset = 0;
    desc->is_dir = false;
    desc->dev_node = SYNTHETIC_DEV_NONE;
    desc->proc_file = SYNTHETIC_PROC_FILE_NONE;
    desc->proc_file_fd_num = -1;
    desc->proc_file_target_pid = -1;
    desc->pty_index = 0;
    desc->pty_is_master = false;
    desc->pipe_endpoint = NULL;
    desc->epoll_instance = NULL;
    memset(&desc->mount_fd, 0, sizeof(desc->mount_fd));
    desc->eventfd_counter = initval;
    desc->eventfd_semaphore = (flags & EFD_SEMAPHORE) != 0;
    desc->synthetic_state = NULL;
    atomic_set(&desc->refs, 1);
    fs_mutex_init(&desc->lock);
    memcpy(desc->path, "anon_inode:[eventfd]", sizeof("anon_inode:[eventfd]"));
    return desc;
}

static int timerfd_clock_allowed(int clockid) {
    return clockid == 0 || clockid == 1;
}

static int timerfd_now_ns(int clockid, uint64_t *now_out) {
    if (!now_out || !timerfd_clock_allowed(clockid)) {
        return -EINVAL;
    }
    return kernel_clock_now_ns(clockid, now_out);
}

static int timerfd_timespec_valid(const struct __kernel_timespec *ts) {
    return ts && ts->tv_sec >= 0 && ts->tv_nsec >= 0 && ts->tv_nsec < 1000000000LL;
}

static uint64_t timerfd_timespec_to_ns(const struct __kernel_timespec *ts) {
    return (uint64_t)ts->tv_sec * 1000000000ULL + (uint64_t)ts->tv_nsec;
}

static void timerfd_ns_to_timespec(uint64_t ns, struct __kernel_timespec *ts) {
    ts->tv_sec = (__kernel_time64_t)(ns / 1000000000ULL);
    ts->tv_nsec = (long long)(ns % 1000000000ULL);
}

static void timerfd_update_locked(fd_description_t *desc, uint64_t now_ns) {
    uint64_t elapsed;
    uint64_t count;

    if (!desc || desc->type != FD_TYPE_TIMERFD || !desc->timerfd_armed ||
        now_ns < desc->timerfd_next_ns) {
        return;
    }

    if (desc->timerfd_interval_ns == 0) {
        desc->timerfd_expirations++;
        desc->timerfd_armed = false;
        desc->timerfd_next_ns = 0;
        return;
    }

    elapsed = now_ns - desc->timerfd_next_ns;
    count = 1 + elapsed / desc->timerfd_interval_ns;
    desc->timerfd_expirations += count;
    desc->timerfd_next_ns += count * desc->timerfd_interval_ns;
}

static void timerfd_snapshot_locked(fd_description_t *desc, uint64_t now_ns,
                                    struct __kernel_itimerspec *value) {
    memset(value, 0, sizeof(*value));
    timerfd_ns_to_timespec(desc->timerfd_interval_ns, &value->it_interval);
    if (desc->timerfd_armed && desc->timerfd_next_ns > now_ns) {
        timerfd_ns_to_timespec(desc->timerfd_next_ns - now_ns, &value->it_value);
    }
}

static fd_description_t *alloc_timerfd_description(int clockid, int flags) {
    fd_description_t *desc = __kmalloc_noprof(sizeof(fd_description_t), GFP_KERNEL | __GFP_ZERO);
    if (!desc) {
        return NULL;
    }

    desc->type = FD_TYPE_TIMERFD;
    desc->fd = -1;
    desc->flags = O_RDWR | (flags & O_NONBLOCK);
    desc->mode = 0;
    desc->offset = 0;
    desc->is_dir = false;
    desc->dev_node = SYNTHETIC_DEV_NONE;
    desc->proc_file = SYNTHETIC_PROC_FILE_NONE;
    desc->proc_file_fd_num = -1;
    desc->proc_file_target_pid = -1;
    desc->pty_index = 0;
    desc->pty_is_master = false;
    desc->pipe_endpoint = NULL;
    desc->epoll_instance = NULL;
    memset(&desc->mount_fd, 0, sizeof(desc->mount_fd));
    desc->timerfd_clockid = clockid;
    desc->synthetic_state = NULL;
    atomic_set(&desc->refs, 1);
    fs_mutex_init(&desc->lock);
    memcpy(desc->path, "anon_inode:[timerfd]", sizeof("anon_inode:[timerfd]"));
    return desc;
}

static fd_description_t *alloc_memfd_description(int real_fd, const char *name, unsigned int flags) {
    fd_description_t *desc;
    int ret;

    desc = __kmalloc_noprof(sizeof(fd_description_t), GFP_KERNEL | __GFP_ZERO);
    if (!desc) {
        return NULL;
    }

    desc->type = FD_TYPE_MEMFD;
    desc->fd = real_fd;
    desc->flags = O_RDWR;
    desc->mode = 0600;
    desc->offset = 0;
    desc->file_identity = (uint64_t)atomic64_inc_return(&fdtable_next_virtual_identity) - 1ULL;
    desc->mount_ns_id = fdtable_current_mount_namespace_id();
    desc->is_dir = false;
    desc->dev_node = SYNTHETIC_DEV_NONE;
    desc->proc_file = SYNTHETIC_PROC_FILE_NONE;
    desc->proc_file_fd_num = -1;
    desc->proc_file_target_pid = -1;
    desc->pty_index = 0;
    desc->pty_is_master = false;
    desc->pipe_endpoint = NULL;
    desc->epoll_instance = NULL;
    memset(&desc->mount_fd, 0, sizeof(desc->mount_fd));
    desc->synthetic_state = NULL;
    desc->memfd_seals = (flags & MFD_ALLOW_SEALING) ? 0 : F_SEAL_SEAL;
    atomic_set(&desc->refs, 1);
    fs_mutex_init(&desc->lock);
    ret = scnprintf(desc->path, sizeof(desc->path), "/memfd:%s", name ? name : "");
    if (ret < 0 || (size_t)ret >= sizeof(desc->path)) {
        fs_mutex_destroy(&desc->lock);
        backing_close(real_fd);
        kfree(desc);
        return NULL;
    }
    return desc;
}

static fd_description_t *alloc_pidfd_description(struct task *task, int flags) {
    fd_description_t *desc;

    if (!task) {
        return NULL;
    }

    desc = __kmalloc_noprof(sizeof(fd_description_t), GFP_KERNEL | __GFP_ZERO);
    if (!desc) {
        return NULL;
    }

    desc->type = FD_TYPE_PIDFD;
    desc->fd = -1;
    desc->flags = O_RDONLY | (flags & O_NONBLOCK);
    desc->mode = 0;
    desc->offset = 0;
    desc->is_dir = false;
    desc->dev_node = SYNTHETIC_DEV_NONE;
    desc->proc_file = SYNTHETIC_PROC_FILE_NONE;
    desc->proc_file_fd_num = -1;
    desc->proc_file_target_pid = -1;
    desc->pty_index = 0;
    desc->pty_is_master = false;
    desc->pipe_endpoint = NULL;
    desc->epoll_instance = NULL;
    memset(&desc->mount_fd, 0, sizeof(desc->mount_fd));
    desc->pidfd_task = task;
    atomic_inc(&task->refs);
    desc->synthetic_state = NULL;
    atomic_set(&desc->refs, 1);
    fs_mutex_init(&desc->lock);
    memcpy(desc->path, "anon_inode:[pidfd]", sizeof("anon_inode:[pidfd]"));
    return desc;
}

static fd_description_t *alloc_mount_fd_description(int flags, const struct vfs_mount_fd *mount_fd) {
    fd_description_t *desc;
    int ret;

    if (!mount_fd || mount_fd->entry_count == 0 || mount_fd->entries[0].target[0] == '\0') {
        return NULL;
    }

    desc = __kmalloc_noprof(sizeof(fd_description_t), GFP_KERNEL | __GFP_ZERO);
    if (!desc) {
        return NULL;
    }

    desc->type = FD_TYPE_MOUNT;
    desc->fd = -1;
    desc->flags = flags;
    desc->mode = 0;
    desc->offset = 0;
    desc->is_dir = true;
    desc->dev_node = SYNTHETIC_DEV_NONE;
    desc->proc_file = SYNTHETIC_PROC_FILE_NONE;
    desc->proc_file_fd_num = -1;
    desc->proc_file_target_pid = -1;
    desc->pty_index = 0;
    desc->pty_is_master = false;
    desc->pipe_endpoint = NULL;
    desc->epoll_instance = NULL;
    memcpy(&desc->mount_fd, mount_fd, sizeof(desc->mount_fd));
    desc->synthetic_state = NULL;
    atomic_set(&desc->refs, 1);
    fs_mutex_init(&desc->lock);
    ret = scnprintf(desc->path, sizeof(desc->path), "mnt:[%s]", mount_fd->entries[0].target);
    if (ret < 0 || (size_t)ret >= sizeof(desc->path)) {
        fs_mutex_destroy(&desc->lock);
        kfree(desc);
        return NULL;
    }
    return desc;
}

static void retain_fd_description(fd_description_t *desc) {
    if (desc) {
        atomic_inc(&desc->refs);
    }
}

static void release_fd_description(fd_description_t *desc) {
    if (!desc) {
        return;
    }
    if (atomic_dec_return(&desc->refs) == 0) {
        if (desc->type == FD_TYPE_HOST || desc->type == FD_TYPE_MEMFD) {
            backing_close(desc->fd);
        } else if (desc->type == FD_TYPE_SYNTHETIC_PTY) {
            pty_close_end_impl(desc->pty_index, desc->pty_is_master);
        } else if (desc->type == FD_TYPE_PIPE) {
            pipe_close_endpoint_impl(desc->pipe_endpoint);
        } else if (desc->type == FD_TYPE_SOCKET) {
            socket_release_impl(desc->socket);
        } else if (desc->type == FD_TYPE_EPOLL) {
            epoll_release_fd_impl(desc->epoll_instance);
        } else if (desc->type == FD_TYPE_PIDFD && desc->pidfd_task) {
            task_put(desc->pidfd_task);
        }
        if (desc->synthetic_state) {
            kfree(desc->synthetic_state);
        }
        fs_mutex_destroy(&desc->lock);
        kfree(desc);
    }
}

static void fd_table_bootstrap_stdio_locked(void) {
    fd_table[STDIN_FILENO].used = true;
    fd_table[STDIN_FILENO].desc = alloc_synthetic_dev_fd_description(O_RDONLY, 0, "/dev/null", SYNTHETIC_DEV_NULL);
    fd_table[STDOUT_FILENO].used = true;
    fd_table[STDOUT_FILENO].desc = alloc_synthetic_dev_fd_description(O_WRONLY, 0, "/dev/null", SYNTHETIC_DEV_NULL);
    fd_table[STDERR_FILENO].used = true;
    fd_table[STDERR_FILENO].desc = alloc_synthetic_dev_fd_description(O_WRONLY, 0, "/dev/null", SYNTHETIC_DEV_NULL);
}

void file_init_impl(void) {
    if (atomic_xchg(&fd_table_initialized, 1) == 1) {
        return;
    }

    fs_mutex_lock(&fd_table_lock);
    memset(fd_table, 0, sizeof(fd_table));
    for (int i = 0; i < NR_OPEN_DEFAULT; i++) {
        fs_mutex_init(&fd_table[i].lock);
    }
    fd_table_bootstrap_stdio_locked();
    fs_mutex_unlock(&fd_table_lock);
}

void file_deinit_impl(void) {
    if (atomic_xchg(&fd_table_initialized, 0) == 0) {
        return;
    }

    fs_mutex_lock(&fd_table_lock);
    for (int i = 0; i < NR_OPEN_DEFAULT; i++) {
        fd_description_t *desc = fd_table[i].desc;
        fd_table[i].desc = NULL;
        fd_table[i].fd_flags = 0;
        fd_table[i].used = false;
        fs_mutex_unlock(&fd_table_lock);
        release_fd_description(desc);
        fs_mutex_lock(&fd_table_lock);
        fs_mutex_destroy(&fd_table[i].lock);
    }
    memset(fd_table, 0, sizeof(fd_table));
    fs_mutex_unlock(&fd_table_lock);
}

int alloc_fd_impl(void) {
    file_init_impl();
    fs_mutex_lock(&fd_table_lock);

    for (int i = 3; i < NR_OPEN_DEFAULT; i++) {
        if (fd_table[i].used && !fdtable_any_task_uses_fd(i)) {
            fd_description_t *desc = fd_table[i].desc;
            fd_table[i].desc = NULL;
            fd_table[i].fd_flags = 0;
            fd_table[i].used = false;
            fs_mutex_unlock(&fd_table_lock);
            release_fd_description(desc);
            fs_mutex_lock(&fd_table_lock);
        }
        if (!fd_table[i].used) {
            fd_table[i].used = true;
            fd_table[i].desc = NULL;
            fd_table[i].fd_flags = 0;
            /* Reserve the fd in the current task immediately to prevent another
             * thread from reclaiming it as "unused" before it is initialized
             * with a description. This mirrors the Linux idea that a newly
             * allocated fd becomes part of the task's table atomically with
             * allocation. */
            fdtable_sync_task_file_locked(i, &fd_table[i]);
            fs_mutex_unlock(&fd_table_lock);
            return i;
        }
    }

    fs_mutex_unlock(&fd_table_lock);
    return -EMFILE;
}

void free_fd_impl(int fd) {
    file_init_impl();
    if (fd < 0 || fd >= NR_OPEN_DEFAULT || fd <= STDERR_FILENO) {
        return;
    }

    fs_mutex_lock(&fd_table_lock);
    if (fd_table[fd].used) {
        fd_description_t *desc = fd_table[fd].desc;
        fd_table[fd].desc = NULL;
        fd_table[fd].fd_flags = 0;
        fd_table[fd].used = false;
        fs_mutex_unlock(&fd_table_lock);
        release_fd_description(desc);
        fdtable_remove_task_file(fd);
        return;
    }
    fs_mutex_unlock(&fd_table_lock);
}

fd_entry_t *get_fd_entry_impl(int fd) {
    struct task *task;
    struct fd_file *file;
    fd_description_t *task_desc;

    if (fd < 0 || fd >= NR_OPEN_DEFAULT) {
        return NULL;
    }

    file_init_impl();
    task = task_current();
    if (task && task->files) {
        fs_mutex_lock(&task->files->lock);
        if ((size_t)fd >= task->files->max_fds || !task->files->fd[fd]) {
            fs_mutex_unlock(&task->files->lock);
            return NULL;
        }
        file = task->files->fd[fd];
        task_desc = (fd_description_t *)file->private_data;
        if (!task_desc) {
            fs_mutex_unlock(&task->files->lock);
            return NULL;
        }
        memset(&fd_task_local_entry, 0, sizeof(fd_task_local_entry));
        fd_task_local_entry.desc = task_desc;
        fd_task_local_entry.fd_flags = (int)file->fd_flags;
        fd_task_local_entry.used = true;
        fd_task_local_entry.task_local = true;
        fd_task_local_entry.task_fd = fd;
        fs_mutex_unlock(&task->files->lock);
        return &fd_task_local_entry;
    }

    int ret = fs_mutex_lock(&fd_table_lock);
    if (ret != 0) {
        return NULL;
    }

    if (!fd_table[fd].used) {
        fs_mutex_unlock(&fd_table_lock);
        return NULL;
    }

    fd_entry_t *entry = &fd_table[fd];
    ret = fs_mutex_lock(&entry->lock);
    if (ret != 0) {
        fs_mutex_unlock(&fd_table_lock);
        return NULL;
    }

    if (!entry->used) {
        fs_mutex_unlock(&entry->lock);
        fs_mutex_unlock(&fd_table_lock);
        return NULL;
    }

    retain_fd_description(entry->desc);
    fs_mutex_unlock(&fd_table_lock);
    return entry;
}

void put_fd_entry_impl(void *entry) {
    if (entry) {
        fd_entry_t *fd_entry = (fd_entry_t *)entry;
        fd_description_t *desc = fd_entry->desc;
        if (!fd_entry->task_local) {
            fs_mutex_unlock(&fd_entry->lock);
        }
        if (!fd_entry->task_local) {
            release_fd_description(desc);
        }
    }
}

int get_real_fd_impl(void *entry) {
    fd_entry_t *fd_entry = (fd_entry_t *)entry;
    return fd_entry->desc ? fd_entry->desc->fd : -1;
}

int get_fd_flags_impl(void *entry) {
    fd_entry_t *fd_entry = (fd_entry_t *)entry;
    return fd_entry->desc ? fd_entry->desc->flags : 0;
}

int get_fd_descriptor_flags_impl(void *entry) {
    return ((fd_entry_t *)entry)->fd_flags;
}

bool get_fd_is_synthetic_dir_impl(void *entry) {
    fd_entry_t *fd_entry = (fd_entry_t *)entry;
    return fd_entry->desc && fd_entry->desc->type == FD_TYPE_SYNTHETIC_DIR;
}

bool get_fd_is_dir_impl(void *entry) {
    fd_entry_t *fd_entry = (fd_entry_t *)entry;
    return fd_entry->desc ? fd_entry->desc->is_dir : false;
}

int get_fd_path_impl(fd_entry_t *entry, char *path, size_t path_len) {
    size_t len;

    if (!entry || !entry->desc || !path || path_len == 0) {
        return -EINVAL;
    }

    len = strlen(entry->desc->path);
    if (len >= path_len) {
        return -ENAMETOOLONG;
    }

    memcpy(path, entry->desc->path, len + 1);
    return 0;
}

uint64_t get_fd_file_identity_impl(void *entry) {
    fd_entry_t *fd_entry = (fd_entry_t *)entry;
    return fd_entry && fd_entry->desc ? fd_entry->desc->file_identity : 0;
}

bool get_fd_path_deleted_impl(void *entry) {
    fd_entry_t *fd_entry = (fd_entry_t *)entry;
    return fd_entry && fd_entry->desc ? fd_entry->desc->path_deleted : false;
}

void set_fd_flags_impl(fd_entry_t *entry, int flags) {
    if (entry && entry->desc) {
        entry->desc->flags = flags;
    }
}

void set_fd_descriptor_flags_impl(fd_entry_t *entry, int flags) {
    if (entry) {
        entry->fd_flags = flags;
        if (entry->task_local) {
            struct task *task = task_current();
            if (task && task->files && entry->task_fd >= 0 &&
                (size_t)entry->task_fd < task->files->max_fds) {
                fs_mutex_lock(&task->files->lock);
                if (task->files->fd[entry->task_fd]) {
                    task->files->fd[entry->task_fd]->fd_flags = (unsigned int)flags;
                }
                fs_mutex_unlock(&task->files->lock);
            }
        }
    }
}

int64_t get_fd_offset_impl(fd_entry_t *entry) {
    if (entry && entry->task_local) {
        struct task *task = task_current();
        int64_t pos = -1;
        if (task && task->files && entry->task_fd >= 0 &&
            (size_t)entry->task_fd < task->files->max_fds) {
            fs_mutex_lock(&task->files->lock);
            if (task->files->fd[entry->task_fd]) {
                pos = task->files->fd[entry->task_fd]->pos;
            }
            fs_mutex_unlock(&task->files->lock);
        }
        return pos;
    }
    return (entry && entry->desc) ? entry->desc->offset : -1;
}

void set_fd_offset_impl(fd_entry_t *entry, int64_t offset) {
    if (entry && entry->desc) {
        entry->desc->offset = offset;
        if (entry->task_local) {
            fdtable_update_file_offsets_for_desc(entry->desc, offset);
        }
    }
}

bool get_fd_is_append_impl(fd_entry_t *entry) {
    return entry && entry->desc && (entry->desc->flags & O_APPEND);
}

bool get_fd_is_readable_impl(void *entry) {
    fd_entry_t *fd_entry = (fd_entry_t *)entry;
    if (!fd_entry || !fd_entry->desc) {
        return false;
    }
    int flags = fd_entry->desc->flags & O_ACCMODE;
    return flags == O_RDONLY || flags == O_RDWR;
}

bool get_fd_is_writable_impl(void *entry) {
    fd_entry_t *fd_entry = (fd_entry_t *)entry;
    if (!fd_entry || !fd_entry->desc) {
        return false;
    }
    int flags = fd_entry->desc->flags & O_ACCMODE;
    return flags == O_WRONLY || flags == O_RDWR;
}

void init_fd_entry_impl(int fd, int real_fd, int flags, uint32_t mode, const char *path) {
    init_fd_entry_with_identity_impl(fd, real_fd, flags, mode, path, 0);
}

void init_fd_entry_with_identity_impl(int fd, int real_fd, int flags, uint32_t mode,
                                      const char *path, uint64_t file_identity) {
    file_init_impl();
    fd_entry_t *entry = &fd_table[fd];
    fs_mutex_lock(&entry->lock);
    entry->desc = alloc_fd_description_with_identity(real_fd, flags, mode, path, file_identity);
    entry->fd_flags = (flags & O_CLOEXEC) ? FD_CLOEXEC : 0;
    fdtable_sync_task_file_locked(fd, entry);
    fs_mutex_unlock(&entry->lock);
}

void init_backing_dirfd_entry_impl(int fd, int real_fd, uint32_t mode, const char *path) {
    init_fd_entry_impl(fd, real_fd, O_RDONLY | O_DIRECTORY, mode, path);
}

int clone_fd_entry_impl(int oldfd, int minfd, bool cloexec) {
    int newfd;
    struct task *task;
    file_init_impl();
    fd_entry_t *old_entry;
    fd_description_t *desc;

    if (minfd < 0 || minfd >= NR_OPEN_DEFAULT) {
        return -EINVAL;
    }

    task = task_current();
    if (task && task->files) {
        struct fd_file *old_file;
        struct fd_file *new_file;

        if (oldfd < 0 || oldfd >= NR_OPEN_DEFAULT || (size_t)oldfd >= task->files->max_fds) {
            return -EBADF;
        }

        fs_mutex_lock(&task->files->lock);
        old_file = task->files->fd[oldfd];
        if (!old_file || !old_file->private_data) {
            fs_mutex_unlock(&task->files->lock);
            return -EBADF;
        }
        newfd = -1;
        for (int i = minfd; (size_t)i < task->files->max_fds; i++) {
            if (!task->files->fd[i]) {
                newfd = i;
                break;
            }
        }
        if (newfd < 0) {
            fs_mutex_unlock(&task->files->lock);
            return -EMFILE;
        }
        new_file = alloc_file();
        if (!new_file) {
            fs_mutex_unlock(&task->files->lock);
            return -ENOMEM;
        }
        desc = (fd_description_t *)old_file->private_data;
        retain_fd_description(desc);
        new_file->fd = newfd;
        new_file->real_fd = old_file->real_fd;
        new_file->flags = old_file->flags;
        new_file->fd_flags = cloexec ? FD_CLOEXEC : 0;
        new_file->pos = (int64_t)desc->offset;
        memcpy(new_file->path, old_file->path, sizeof(new_file->path));
        new_file->private_data = desc;
        task->files->fd[newfd] = new_file;
        fs_mutex_unlock(&task->files->lock);
        return newfd;
    }

    fs_mutex_lock(&fd_table_lock);
    if (!fd_table[oldfd].used || !fd_table[oldfd].desc) {
        fs_mutex_unlock(&fd_table_lock);
        return -EBADF;
    }

    old_entry = &fd_table[oldfd];
    desc = old_entry->desc;
    retain_fd_description(desc);

    newfd = -1;
    for (int i = minfd; i < NR_OPEN_DEFAULT; i++) {
        if (!fd_table[i].used) {
            fd_table[i].used = true;
            fd_table[i].desc = desc;
            fd_table[i].fd_flags = cloexec ? FD_CLOEXEC : 0;
            fdtable_sync_task_file_locked(i, &fd_table[i]);
            newfd = i;
            break;
        }
    }
    fs_mutex_unlock(&fd_table_lock);

    if (newfd < 0) {
        release_fd_description(desc);
        return -EMFILE;
    }

    return newfd;
}

int pidfd_getfd_impl(struct task *target, int targetfd, unsigned int flags) {
    struct task *current;
    struct fd_table *source_files;
    struct fd_table *dest_files;
    struct fd_file *source_file;
    struct fd_file *new_file;
    fd_description_t *desc;
    int newfd = -1;
    bool same_files;

    if (flags != 0) {
        return -EINVAL;
    }
    if (!target || targetfd < 0) {
        return target ? -EBADF : -ESRCH;
    }
    if (atomic_read(&target->exited) != 0) {
        return -ESRCH;
    }

    current = task_current();
    if (!current || !current->files || !target->files) {
        return -ESRCH;
    }

    source_files = target->files;
    dest_files = current->files;
    if ((size_t)targetfd >= source_files->max_fds) {
        return -EBADF;
    }

    same_files = source_files == dest_files;
    if (same_files) {
        fs_mutex_lock(&source_files->lock);
    } else if (source_files < dest_files) {
        fs_mutex_lock(&source_files->lock);
        fs_mutex_lock(&dest_files->lock);
    } else {
        fs_mutex_lock(&dest_files->lock);
        fs_mutex_lock(&source_files->lock);
    }

    source_file = source_files->fd[targetfd];
    if (!source_file || !source_file->private_data) {
        newfd = -EBADF;
        goto out_unlock;
    }

    for (size_t i = 0; i < dest_files->max_fds; i++) {
        if (!dest_files->fd[i]) {
            newfd = (int)i;
            break;
        }
    }
    if (newfd < 0) {
        newfd = -EMFILE;
        goto out_unlock;
    }

    new_file = alloc_file();
    if (!new_file) {
        newfd = -ENOMEM;
        goto out_unlock;
    }

    desc = (fd_description_t *)source_file->private_data;
    retain_fd_description(desc);
    new_file->fd = newfd;
    new_file->real_fd = source_file->real_fd;
    new_file->flags = source_file->flags;
    new_file->fd_flags = FD_CLOEXEC;
        new_file->pos = (int64_t)desc->offset;
    memcpy(new_file->path, source_file->path, sizeof(new_file->path));
    new_file->private_data = desc;
    dest_files->fd[newfd] = new_file;

out_unlock:
    if (same_files) {
        fs_mutex_unlock(&source_files->lock);
    } else {
        fs_mutex_unlock(&source_files->lock);
        fs_mutex_unlock(&dest_files->lock);
    }

    return newfd;
}

int replace_fd_entry_impl(int newfd, int oldfd, bool cloexec) {
    fd_description_t *old_desc;
    struct task *task;
    file_init_impl();
    fd_description_t *new_desc;

    if (newfd < 0 || newfd >= NR_OPEN_DEFAULT || oldfd < 0 || oldfd >= NR_OPEN_DEFAULT) {
        return -EBADF;
    }

    task = task_current();
    if (task && task->files) {
        struct fd_file *old_file;
        struct fd_file *new_file;
        struct fd_file *replaced_file = NULL;

        if ((size_t)oldfd >= task->files->max_fds || (size_t)newfd >= task->files->max_fds) {
            return -EBADF;
        }
        fs_mutex_lock(&task->files->lock);
        old_file = task->files->fd[oldfd];
        if (!old_file || !old_file->private_data) {
            fs_mutex_unlock(&task->files->lock);
            return -EBADF;
        }
        new_file = alloc_file();
        if (!new_file) {
            fs_mutex_unlock(&task->files->lock);
            return -ENOMEM;
        }
        old_desc = (fd_description_t *)old_file->private_data;
        retain_fd_description(old_desc);
        new_file->fd = newfd;
        new_file->real_fd = old_file->real_fd;
        new_file->flags = old_file->flags;
        new_file->fd_flags = cloexec ? FD_CLOEXEC : 0;
        new_file->pos = (int64_t)old_desc->offset;
        memcpy(new_file->path, old_file->path, sizeof(new_file->path));
        new_file->private_data = old_desc;
        replaced_file = task->files->fd[newfd];
        task->files->fd[newfd] = new_file;
        fs_mutex_unlock(&task->files->lock);
        free_file(replaced_file);
        return newfd;
    }

    fs_mutex_lock(&fd_table_lock);
    if (!fd_table[oldfd].used || !fd_table[oldfd].desc) {
        fs_mutex_unlock(&fd_table_lock);
        return -EBADF;
    }

    old_desc = fd_table[oldfd].desc;
    retain_fd_description(old_desc);

    new_desc = fd_table[newfd].used ? fd_table[newfd].desc : NULL;
    fd_table[newfd].used = true;
    fd_table[newfd].desc = old_desc;
    fd_table[newfd].fd_flags = cloexec ? FD_CLOEXEC : 0;
    fdtable_sync_task_file_locked(newfd, &fd_table[newfd]);
    fs_mutex_unlock(&fd_table_lock);

    release_fd_description(new_desc);
    return newfd;
}

int close_impl(int fd) {
    struct task *task;

    file_init_impl();
    if (fd < 0 || fd >= NR_OPEN_DEFAULT) {
        return -EBADF;
    }
    task = task_current();
    if (task && task->files) {
        if (free_fd(task->files, fd) != 0) {
            return -1;
        }
        if (!fdtable_any_task_uses_fd(fd)) {
            free_fd_impl(fd);
        }
        return 0;
    }
    if (!fd_table[fd].used) {
        return -EBADF;
    }
    free_fd_impl(fd);
    return 0;
}

int close_range_impl(unsigned int first, unsigned int last, unsigned int flags) {
    struct task *task;
    unsigned int allowed_flags = CLOSE_RANGE_UNSHARE | CLOSE_RANGE_CLOEXEC;
    unsigned int upper;

    file_init_impl();
    if ((flags & ~allowed_flags) != 0 || first > last) {
        return -EINVAL;
    }
    if (first >= NR_OPEN_DEFAULT) {
        return 0;
    }
    upper = last >= NR_OPEN_DEFAULT ? (unsigned int)NR_OPEN_DEFAULT - 1 : last;

    task = task_current();
    if ((flags & CLOSE_RANGE_CLOEXEC) != 0) {
        if (task && task->files) {
            fs_mutex_lock(&task->files->lock);
            for (unsigned int fd = first; fd <= upper && (size_t)fd < task->files->max_fds; fd++) {
                if (task->files->fd[fd]) {
                    task->files->fd[fd]->fd_flags |= FD_CLOEXEC;
                }
            }
            fs_mutex_unlock(&task->files->lock);
            return 0;
        }
        fs_mutex_lock(&fd_table_lock);
        for (unsigned int fd = first; fd <= upper; fd++) {
            if (fd_table[fd].used) {
                fd_table[fd].fd_flags |= FD_CLOEXEC;
            }
        }
        fs_mutex_unlock(&fd_table_lock);
        return 0;
    }

    for (unsigned int fd = first; fd <= upper; fd++) {
        int ret = close_impl((int)fd);
        if (ret != 0 && ret != -EBADF) {
            return ret;
        }
    }
    return 0;
}

int close_on_exec_impl(void) {
    fd_description_t *descs_to_release[NR_OPEN_DEFAULT];
    int fds_to_remove[NR_OPEN_DEFAULT];
    int release_count = 0;
    int closed = 0;
    struct task *task;

    file_init_impl();
    task = task_current();
    if (task && task->files) {
        int closed_fds[NR_OPEN_DEFAULT];

        fs_mutex_lock(&task->files->lock);
        for (int fd = 0; fd < NR_OPEN_DEFAULT && (size_t)fd < task->files->max_fds; fd++) {
            if (task->files->fd[fd] && (task->files->fd[fd]->fd_flags & FD_CLOEXEC)) {
                struct fd_file *file = task->files->fd[fd];
                task->files->fd[fd] = NULL;
                closed_fds[closed++] = fd;
                free_file(file);
            }
        }
        fs_mutex_unlock(&task->files->lock);
        for (int i = 0; i < closed; i++) {
            if (!fdtable_any_task_uses_fd(closed_fds[i])) {
                free_fd_impl(closed_fds[i]);
            }
        }
        return closed;
    }

    fs_mutex_lock(&fd_table_lock);
    for (int fd = 0; fd < NR_OPEN_DEFAULT; fd++) {
        if (!fd_table[fd].used || (fd_table[fd].fd_flags & FD_CLOEXEC) == 0) {
            continue;
        }

        descs_to_release[release_count++] = fd_table[fd].desc;
        fds_to_remove[closed] = fd;
        fd_table[fd].desc = NULL;
        fd_table[fd].fd_flags = 0;
        fd_table[fd].used = false;
        closed++;
    }
    fs_mutex_unlock(&fd_table_lock);

    for (int i = 0; i < release_count; i++) {
        release_fd_description(descs_to_release[i]);
    }
    for (int i = 0; i < closed; i++) {
        fdtable_remove_task_file(fds_to_remove[i]);
    }

    return closed;
}

void init_synthetic_fd_entry_impl(int fd, int flags, uint32_t mode, const char *path) {
    file_init_impl();
    fd_entry_t *entry = &fd_table[fd];
    fs_mutex_lock(&entry->lock);
    entry->desc = alloc_synthetic_subdir_fd_description(flags, mode, path, SYNTHETIC_DIR_ROOT);
    entry->fd_flags = (flags & O_CLOEXEC) ? FD_CLOEXEC : 0;
    fdtable_sync_task_file_locked(fd, entry);
    fs_mutex_unlock(&entry->lock);
}

void init_synthetic_dev_fd_entry_impl(int fd, int flags, uint32_t mode, const char *path, synthetic_dev_node_t dev_node) {
    file_init_impl();
    fd_entry_t *entry = &fd_table[fd];
    fs_mutex_lock(&entry->lock);
    entry->desc = alloc_synthetic_dev_fd_description(flags, mode, path, dev_node);
    entry->fd_flags = (flags & O_CLOEXEC) ? FD_CLOEXEC : 0;
    fdtable_sync_task_file_locked(fd, entry);
    fs_mutex_unlock(&entry->lock);
}

void init_synthetic_pty_fd_entry_impl(int fd, int flags, uint32_t mode, const char *path,
                                      unsigned int pty_index, bool is_master) {
    file_init_impl();
    fd_entry_t *entry = &fd_table[fd];
    fs_mutex_lock(&entry->lock);
    entry->desc = alloc_synthetic_pty_fd_description(flags, mode, path, pty_index, is_master);
    entry->fd_flags = (flags & O_CLOEXEC) ? FD_CLOEXEC : 0;
    fdtable_sync_task_file_locked(fd, entry);
    fs_mutex_unlock(&entry->lock);
}

int init_pipe_fd_entry_impl(int fd, int flags, struct pipe_endpoint *endpoint) {
    file_init_impl();
    fd_entry_t *entry = &fd_table[fd];
    fs_mutex_lock(&entry->lock);
    entry->desc = alloc_pipe_fd_description(flags, endpoint);
    if (!entry->desc) {
        fs_mutex_unlock(&entry->lock);
        return -1;
    }
    entry->fd_flags = (flags & O_CLOEXEC) ? FD_CLOEXEC : 0;
    fdtable_sync_task_file_locked(fd, entry);
    fs_mutex_unlock(&entry->lock);
    return 0;
}

int init_socket_fd_entry_impl(int fd, int flags, struct socket_state *socket) {
    file_init_impl();
    fd_entry_t *entry = &fd_table[fd];
    fs_mutex_lock(&entry->lock);
    entry->desc = alloc_socket_fd_description(flags, socket);
    if (!entry->desc) {
        fs_mutex_unlock(&entry->lock);
        return -1;
    }
    entry->fd_flags = (flags & O_CLOEXEC) ? FD_CLOEXEC : 0;
    fdtable_sync_task_file_locked(fd, entry);
    fs_mutex_unlock(&entry->lock);
    return 0;
}

int init_epoll_fd_entry_impl(int fd, int flags, struct epoll_instance *instance) {
    file_init_impl();
    fd_entry_t *entry = &fd_table[fd];
    fs_mutex_lock(&entry->lock);
    entry->desc = alloc_epoll_fd_description(flags, instance);
    if (!entry->desc) {
        fs_mutex_unlock(&entry->lock);
        return -1;
    }
    entry->fd_flags = (flags & O_CLOEXEC) ? FD_CLOEXEC : 0;
    fdtable_sync_task_file_locked(fd, entry);
    fs_mutex_unlock(&entry->lock);
    return 0;
}

int init_mount_fd_entry_impl(int fd, int flags, const struct vfs_mount_fd *mount_fd) {
    file_init_impl();
    fd_entry_t *entry = &fd_table[fd];
    fs_mutex_lock(&entry->lock);
    entry->desc = alloc_mount_fd_description(flags, mount_fd);
    if (!entry->desc) {
        fs_mutex_unlock(&entry->lock);
        return -ENOMEM;
    }
    entry->fd_flags = (flags & O_CLOEXEC) ? FD_CLOEXEC : 0;
    fdtable_sync_task_file_locked(fd, entry);
    fs_mutex_unlock(&entry->lock);
    return 0;
}

static int init_eventfd_entry_impl(int fd, unsigned int initval, int flags) {
    file_init_impl();
    fd_entry_t *entry = &fd_table[fd];
    fs_mutex_lock(&entry->lock);
    entry->desc = alloc_eventfd_description(initval, flags);
    if (!entry->desc) {
        fs_mutex_unlock(&entry->lock);
        return -1;
    }
    entry->fd_flags = (flags & O_CLOEXEC) ? FD_CLOEXEC : 0;
    fdtable_sync_task_file_locked(fd, entry);
    fs_mutex_unlock(&entry->lock);
    return 0;
}

int eventfd2_impl(unsigned int initval, int flags) {
    const int allowed_flags = EFD_CLOEXEC | EFD_NONBLOCK | EFD_SEMAPHORE;
    int fd;

    if ((flags & ~allowed_flags) != 0) {
        return -EINVAL;
    }

    fd = alloc_fd_impl();
    if (fd < 0) {
        return -1;
    }
    if (init_eventfd_entry_impl(fd, initval, flags) != 0) {
        free_fd_impl(fd);
        return -1;
    }
    return fd;
}

static int init_timerfd_entry_impl(int fd, int clockid, int flags) {
    file_init_impl();
    fd_entry_t *entry = &fd_table[fd];
    fs_mutex_lock(&entry->lock);
    entry->desc = alloc_timerfd_description(clockid, flags);
    if (!entry->desc) {
        fs_mutex_unlock(&entry->lock);
        return -1;
    }
    entry->fd_flags = (flags & O_CLOEXEC) ? FD_CLOEXEC : 0;
    fdtable_sync_task_file_locked(fd, entry);
    fs_mutex_unlock(&entry->lock);
    return 0;
}

static int init_memfd_entry_impl(int fd, const char *name, unsigned int flags, int real_fd) {
    file_init_impl();
    fd_entry_t *entry = &fd_table[fd];
    fs_mutex_lock(&entry->lock);
    entry->desc = alloc_memfd_description(real_fd, name, flags);
    if (!entry->desc) {
        fs_mutex_unlock(&entry->lock);
        return -1;
    }
    entry->fd_flags = (flags & MFD_CLOEXEC) ? FD_CLOEXEC : 0;
    fdtable_sync_task_file_locked(fd, entry);
    fs_mutex_unlock(&entry->lock);
    return 0;
}

static int init_pidfd_entry_impl(int fd, struct task *task, int flags) {
    file_init_impl();
    fd_entry_t *entry = &fd_table[fd];
    fs_mutex_lock(&entry->lock);
    entry->desc = alloc_pidfd_description(task, flags);
    if (!entry->desc) {
        fs_mutex_unlock(&entry->lock);
        return -1;
    }
    entry->fd_flags = (flags & O_CLOEXEC) ? FD_CLOEXEC : 0;
    fdtable_sync_task_file_locked(fd, entry);
    fs_mutex_unlock(&entry->lock);
    return 0;
}

int timerfd_create_impl(int clockid, int flags) {
    const int allowed_flags = TFD_CLOEXEC | TFD_NONBLOCK;
    int fd;

    if (!timerfd_clock_allowed(clockid) || (flags & ~allowed_flags) != 0) {
        return -EINVAL;
    }

    fd = alloc_fd_impl();
    if (fd < 0) {
        return -1;
    }
    if (init_timerfd_entry_impl(fd, clockid, flags) != 0) {
        free_fd_impl(fd);
        return -1;
    }
    return fd;
}

int memfd_create_impl(const char *name, unsigned int flags) {
    unsigned int allowed_flags = MFD_CLOEXEC | MFD_ALLOW_SEALING;
    int fd;
    int real_fd;

    if (!name) {
        return -EFAULT;
    }
    if ((flags & ~allowed_flags) != 0) {
        return -EINVAL;
    }

    real_fd = backing_memfd_create();
    if (real_fd < 0) {
        return -1;
    }
    fd = alloc_fd_impl();
    if (fd < 0) {
        backing_close(real_fd);
        return -1;
    }
    if (init_memfd_entry_impl(fd, name, flags, real_fd) != 0) {
        free_fd_impl(fd);
        return -1;
    }
    return fd;
}

int pidfd_create_for_task_impl(struct task *task, int flags) {
    const int allowed_flags = O_CLOEXEC | O_NONBLOCK;
    int fd;

    if (!task) {
        return -ESRCH;
    }
    if ((flags & ~allowed_flags) != 0) {
        return -EINVAL;
    }

    fd = alloc_fd_impl();
    if (fd < 0) {
        return -1;
    }
    if (init_pidfd_entry_impl(fd, task, flags) != 0) {
        free_fd_impl(fd);
        return -1;
    }
    return fd;
}

int pidfd_open_impl(int32_t pid, unsigned int flags) {
    struct task *task;
    int fd;
    unsigned int allowed_flags = PIDFD_NONBLOCK;

    if ((flags & ~allowed_flags) != 0 || (flags & PIDFD_THREAD) != 0) {
        return -EINVAL;
    }

    task = task_lookup(pid);
    if (!task) {
        return -ESRCH;
    }

    fd = pidfd_create_for_task_impl(task, (flags & PIDFD_NONBLOCK) ? O_NONBLOCK : 0);
    task_put(task);
    return fd;
}

bool get_fd_is_synthetic_dev_impl(void *entry) {
    fd_entry_t *fd_entry = (fd_entry_t *)entry;
    return fd_entry->desc && fd_entry->desc->type == FD_TYPE_SYNTHETIC_DEV;
}

synthetic_dev_node_t get_fd_synthetic_dev_node_impl(void *entry) {
    fd_entry_t *fd_entry = (fd_entry_t *)entry;
    return fd_entry->desc ? fd_entry->desc->dev_node : SYNTHETIC_DEV_NONE;
}

bool get_fd_is_synthetic_pty_impl(void *entry) {
    fd_entry_t *fd_entry = (fd_entry_t *)entry;
    return fd_entry->desc && fd_entry->desc->type == FD_TYPE_SYNTHETIC_PTY;
}

bool get_fd_is_synthetic_pty_master_impl(void *entry) {
    fd_entry_t *fd_entry = (fd_entry_t *)entry;
    return fd_entry->desc && fd_entry->desc->type == FD_TYPE_SYNTHETIC_PTY && fd_entry->desc->pty_is_master;
}

unsigned int get_fd_synthetic_pty_index_impl(void *entry) {
    fd_entry_t *fd_entry = (fd_entry_t *)entry;
    return fd_entry->desc ? fd_entry->desc->pty_index : 0;
}

bool get_fd_is_pipe_impl(void *entry) {
    fd_entry_t *fd_entry = (fd_entry_t *)entry;
    return fd_entry->desc && fd_entry->desc->type == FD_TYPE_PIPE;
}

struct pipe_endpoint *get_fd_pipe_endpoint_impl(void *entry) {
    fd_entry_t *fd_entry = (fd_entry_t *)entry;
    return fd_entry->desc ? fd_entry->desc->pipe_endpoint : NULL;
}

bool get_fd_is_socket_impl(void *entry) {
    fd_entry_t *fd_entry = (fd_entry_t *)entry;
    return fd_entry->desc && fd_entry->desc->type == FD_TYPE_SOCKET;
}

struct socket_state *get_fd_socket_impl(void *entry) {
    fd_entry_t *fd_entry = (fd_entry_t *)entry;
    return fd_entry->desc ? fd_entry->desc->socket : NULL;
}

bool get_fd_is_epoll_impl(void *entry) {
    fd_entry_t *fd_entry = (fd_entry_t *)entry;
    return fd_entry->desc && fd_entry->desc->type == FD_TYPE_EPOLL;
}

struct epoll_instance *get_fd_epoll_instance_impl(void *entry) {
    fd_entry_t *fd_entry = (fd_entry_t *)entry;
    return fd_entry->desc ? fd_entry->desc->epoll_instance : NULL;
}

bool get_fd_is_mount_impl(void *entry) {
    fd_entry_t *fd_entry = (fd_entry_t *)entry;
    return fd_entry->desc && fd_entry->desc->type == FD_TYPE_MOUNT;
}

int get_fd_mount_impl(void *entry, struct vfs_mount_fd *mount_fd) {
    fd_entry_t *fd_entry = (fd_entry_t *)entry;

    if (!fd_entry || !fd_entry->desc || fd_entry->desc->type != FD_TYPE_MOUNT || !mount_fd) {
        return -EINVAL;
    }

    memcpy(mount_fd, &fd_entry->desc->mount_fd, sizeof(*mount_fd));
    return 0;
}

bool get_fd_is_eventfd_impl(void *entry) {
    fd_entry_t *fd_entry = (fd_entry_t *)entry;
    return fd_entry->desc && fd_entry->desc->type == FD_TYPE_EVENTFD;
}

long eventfd_read_entry_impl(void *entry, void *buf, size_t count) {
    fd_entry_t *fd_entry = (fd_entry_t *)entry;
    fd_description_t *desc;
    uint64_t value;

    if (!fd_entry || !fd_entry->desc || fd_entry->desc->type != FD_TYPE_EVENTFD) {
        return -EBADF;
    }
    if (!buf) {
        return -EFAULT;
    }
    if (count < sizeof(uint64_t)) {
        return -EINVAL;
    }

    desc = fd_entry->desc;
    fs_mutex_lock(&desc->lock);
    if (desc->eventfd_counter == 0) {
        fs_mutex_unlock(&desc->lock);
        return -EAGAIN;
    }
    if (desc->eventfd_semaphore) {
        value = 1;
        desc->eventfd_counter--;
    } else {
        value = desc->eventfd_counter;
        desc->eventfd_counter = 0;
    }
    fs_mutex_unlock(&desc->lock);

    memcpy(buf, &value, sizeof(value));
    poll_notify_readiness_impl();
    return (long)sizeof(value);
}

long eventfd_write_entry_impl(void *entry, const void *buf, size_t count) {
    fd_entry_t *fd_entry = (fd_entry_t *)entry;
    fd_description_t *desc;
    uint64_t value;

    if (!fd_entry || !fd_entry->desc || fd_entry->desc->type != FD_TYPE_EVENTFD) {
        return -EBADF;
    }
    if (!buf) {
        return -EFAULT;
    }
    if (count < sizeof(uint64_t)) {
        return -EINVAL;
    }
    memcpy(&value, buf, sizeof(value));
    if (value == U64_MAX) {
        return -EINVAL;
    }

    desc = fd_entry->desc;
    fs_mutex_lock(&desc->lock);
    if (U64_MAX - 1 - desc->eventfd_counter < value) {
        fs_mutex_unlock(&desc->lock);
        return -EAGAIN;
    }
    desc->eventfd_counter += value;
    fs_mutex_unlock(&desc->lock);

    poll_notify_readiness_impl();
    return (long)sizeof(value);
}

bool eventfd_read_ready_entry_impl(void *entry) {
    fd_entry_t *fd_entry = (fd_entry_t *)entry;
    fd_description_t *desc;
    bool ready;

    if (!fd_entry || !fd_entry->desc || fd_entry->desc->type != FD_TYPE_EVENTFD) {
        return false;
    }

    desc = fd_entry->desc;
    fs_mutex_lock(&desc->lock);
    ready = desc->eventfd_counter > 0;
    fs_mutex_unlock(&desc->lock);
    return ready;
}

bool eventfd_write_ready_entry_impl(void *entry) {
    fd_entry_t *fd_entry = (fd_entry_t *)entry;
    fd_description_t *desc;
    bool ready;

    if (!fd_entry || !fd_entry->desc || fd_entry->desc->type != FD_TYPE_EVENTFD) {
        return false;
    }

    desc = fd_entry->desc;
    fs_mutex_lock(&desc->lock);
    ready = desc->eventfd_counter < U64_MAX - 1;
    fs_mutex_unlock(&desc->lock);
    return ready;
}

bool get_fd_is_timerfd_impl(void *entry) {
    fd_entry_t *fd_entry = (fd_entry_t *)entry;
    return fd_entry->desc && fd_entry->desc->type == FD_TYPE_TIMERFD;
}

static int timerfd_entry_snapshot(void *entry, struct __kernel_itimerspec *value) {
    fd_entry_t *fd_entry = (fd_entry_t *)entry;
    fd_description_t *desc;
    uint64_t now_ns;

    if (!fd_entry || !fd_entry->desc || fd_entry->desc->type != FD_TYPE_TIMERFD || !value) {
        return -EINVAL;
    }

    desc = fd_entry->desc;
    if (timerfd_now_ns(desc->timerfd_clockid, &now_ns) != 0) {
        return -1;
    }
    fs_mutex_lock(&desc->lock);
    timerfd_update_locked(desc, now_ns);
    timerfd_snapshot_locked(desc, now_ns, value);
    fs_mutex_unlock(&desc->lock);
    return 0;
}

int timerfd_gettime_impl(int fd, struct __kernel_itimerspec *curr_value) {
    void *entry;
    int ret;

    if (!curr_value) {
        return -EFAULT;
    }
    entry = get_fd_entry_impl(fd);
    if (!entry) {
        return -1;
    }
    ret = timerfd_entry_snapshot(entry, curr_value);
    put_fd_entry_impl(entry);
    return ret;
}

int timerfd_settime_impl(int fd, int flags, const struct __kernel_itimerspec *new_value,
                         struct __kernel_itimerspec *old_value) {
    const int allowed_flags = TFD_TIMER_ABSTIME | TFD_TIMER_CANCEL_ON_SET;
    void *entry;
    fd_entry_t *fd_entry;
    fd_description_t *desc;
    uint64_t now_ns;
    uint64_t value_ns;
    int ret = 0;

    if ((flags & ~allowed_flags) != 0) {
        return -EINVAL;
    }
    if (!new_value) {
        return -EFAULT;
    }
    if (!timerfd_timespec_valid(&new_value->it_value) ||
        !timerfd_timespec_valid(&new_value->it_interval)) {
        return -EINVAL;
    }

    entry = get_fd_entry_impl(fd);
    if (!entry) {
        return -1;
    }
    fd_entry = (fd_entry_t *)entry;
    if (!fd_entry->desc || fd_entry->desc->type != FD_TYPE_TIMERFD) {
        put_fd_entry_impl(entry);
        return -EINVAL;
    }

    desc = fd_entry->desc;
    if (timerfd_now_ns(desc->timerfd_clockid, &now_ns) != 0) {
        put_fd_entry_impl(entry);
        return -1;
    }
    fs_mutex_lock(&desc->lock);
    timerfd_update_locked(desc, now_ns);
    if (old_value) {
        timerfd_snapshot_locked(desc, now_ns, old_value);
    }

    value_ns = timerfd_timespec_to_ns(&new_value->it_value);
    desc->timerfd_interval_ns = timerfd_timespec_to_ns(&new_value->it_interval);
    desc->timerfd_expirations = 0;
    if (value_ns == 0) {
        desc->timerfd_armed = false;
        desc->timerfd_next_ns = 0;
    } else {
        desc->timerfd_armed = true;
        desc->timerfd_next_ns = (flags & TFD_TIMER_ABSTIME) ? value_ns : now_ns + value_ns;
    }
    fs_mutex_unlock(&desc->lock);
    put_fd_entry_impl(entry);

    poll_notify_readiness_impl();
    return ret;
}

long timerfd_read_entry_impl(void *entry, void *buf, size_t count) {
    fd_entry_t *fd_entry = (fd_entry_t *)entry;
    fd_description_t *desc;
    uint64_t now_ns;
    uint64_t expirations;

    if (!fd_entry || !fd_entry->desc || fd_entry->desc->type != FD_TYPE_TIMERFD) {
        return -EBADF;
    }
    if (!buf) {
        return -EFAULT;
    }
    if (count < sizeof(uint64_t)) {
        return -EINVAL;
    }

    desc = fd_entry->desc;
    if (timerfd_now_ns(desc->timerfd_clockid, &now_ns) != 0) {
        return -1;
    }
    fs_mutex_lock(&desc->lock);
    timerfd_update_locked(desc, now_ns);
    expirations = desc->timerfd_expirations;
    if (expirations == 0) {
        fs_mutex_unlock(&desc->lock);
        return -EAGAIN;
    }
    desc->timerfd_expirations = 0;
    fs_mutex_unlock(&desc->lock);

    memcpy(buf, &expirations, sizeof(expirations));
    poll_notify_readiness_impl();
    return (long)sizeof(expirations);
}

long timerfd_write_entry_impl(void *entry, const void *buf, size_t count) {
    (void)entry;
    (void)buf;
    (void)count;
    return -EINVAL;
}

bool timerfd_read_ready_entry_impl(void *entry) {
    fd_entry_t *fd_entry = (fd_entry_t *)entry;
    fd_description_t *desc;
    uint64_t now_ns;
    bool ready;

    if (!fd_entry || !fd_entry->desc || fd_entry->desc->type != FD_TYPE_TIMERFD) {
        return false;
    }

    desc = fd_entry->desc;
    if (timerfd_now_ns(desc->timerfd_clockid, &now_ns) != 0) {
        return false;
    }
    fs_mutex_lock(&desc->lock);
    timerfd_update_locked(desc, now_ns);
    ready = desc->timerfd_expirations > 0;
    fs_mutex_unlock(&desc->lock);
    return ready;
}

bool get_fd_is_memfd_impl(void *entry) {
    fd_entry_t *fd_entry = (fd_entry_t *)entry;
    return fd_entry->desc && fd_entry->desc->type == FD_TYPE_MEMFD;
}

bool get_fd_is_pidfd_impl(void *entry) {
    fd_entry_t *fd_entry = (fd_entry_t *)entry;
    return fd_entry->desc && fd_entry->desc->type == FD_TYPE_PIDFD;
}

struct task *pidfd_get_task_entry_impl(void *entry) {
    fd_entry_t *fd_entry = (fd_entry_t *)entry;
    struct task *task;

    if (!fd_entry || !fd_entry->desc || fd_entry->desc->type != FD_TYPE_PIDFD ||
        !fd_entry->desc->pidfd_task) {
        return NULL;
    }

    task = fd_entry->desc->pidfd_task;
    atomic_inc(&task->refs);
    return task;
}

bool pidfd_read_ready_entry_impl(void *entry) {
    fd_entry_t *fd_entry = (fd_entry_t *)entry;

    if (!fd_entry || !fd_entry->desc || fd_entry->desc->type != FD_TYPE_PIDFD ||
        !fd_entry->desc->pidfd_task) {
        return false;
    }

    return atomic_read(&fd_entry->desc->pidfd_task->exited) != 0;
}

int memfd_get_seals_entry_impl(void *entry) {
    fd_entry_t *fd_entry = (fd_entry_t *)entry;
    int seals;

    if (!fd_entry || !fd_entry->desc || fd_entry->desc->type != FD_TYPE_MEMFD) {
        return -EINVAL;
    }
    fs_mutex_lock(&fd_entry->desc->lock);
    seals = fd_entry->desc->memfd_seals;
    fs_mutex_unlock(&fd_entry->desc->lock);
    return seals;
}

int memfd_add_seals_entry_impl(void *entry, int seals) {
    fd_entry_t *fd_entry = (fd_entry_t *)entry;
    fd_description_t *desc;
    int allowed_seals = F_SEAL_SEAL | F_SEAL_SHRINK | F_SEAL_GROW |
                        F_SEAL_WRITE | F_SEAL_FUTURE_WRITE | F_SEAL_EXEC;

    if (!fd_entry || !fd_entry->desc || fd_entry->desc->type != FD_TYPE_MEMFD) {
        return -EINVAL;
    }
    if ((seals & ~allowed_seals) != 0) {
        return -EINVAL;
    }

    desc = fd_entry->desc;
    fs_mutex_lock(&desc->lock);
    if ((desc->memfd_seals & F_SEAL_SEAL) != 0) {
        fs_mutex_unlock(&desc->lock);
        return -EPERM;
    }
    desc->memfd_seals |= seals;
    fs_mutex_unlock(&desc->lock);
    return 0;
}

int memfd_write_allowed_entry_impl(void *entry, int64_t offset, size_t count) {
    fd_entry_t *fd_entry = (fd_entry_t *)entry;
    fd_description_t *desc;
    (void)offset;
    (void)count;

    if (!fd_entry || !fd_entry->desc || fd_entry->desc->type != FD_TYPE_MEMFD) {
        return 0;
    }
    desc = fd_entry->desc;
    fs_mutex_lock(&desc->lock);
    if ((desc->memfd_seals & (F_SEAL_WRITE | F_SEAL_FUTURE_WRITE)) != 0) {
        fs_mutex_unlock(&desc->lock);
        return -EPERM;
    }
    fs_mutex_unlock(&desc->lock);
    return 0;
}

int memfd_truncate_allowed_entry_impl(void *entry, int64_t length) {
    fd_entry_t *fd_entry = (fd_entry_t *)entry;
    fd_description_t *desc;
    struct stat st;

    if (!fd_entry || !fd_entry->desc || fd_entry->desc->type != FD_TYPE_MEMFD) {
        return 0;
    }
    if (backing_fstat(get_real_fd_impl(entry), &st) != 0) {
        return -EIO;
    }

    desc = fd_entry->desc;
    fs_mutex_lock(&desc->lock);
    if ((desc->memfd_seals & F_SEAL_GROW) != 0 && length > (int64_t)st.st_size) {
        fs_mutex_unlock(&desc->lock);
        return -EPERM;
    }
    if ((desc->memfd_seals & F_SEAL_SHRINK) != 0 && length < (int64_t)st.st_size) {
        fs_mutex_unlock(&desc->lock);
        return -EPERM;
    }
    if ((desc->memfd_seals & (F_SEAL_WRITE | F_SEAL_FUTURE_WRITE)) != 0) {
        fs_mutex_unlock(&desc->lock);
        return -EPERM;
    }
    fs_mutex_unlock(&desc->lock);
    return 0;
}

void init_synthetic_proc_file_fd_entry_impl(int fd, int flags, uint32_t mode, const char *path, synthetic_proc_file_t proc_file) {
    file_init_impl();
    fd_entry_t *entry = &fd_table[fd];
    fs_mutex_lock(&entry->lock);
    entry->desc = alloc_synthetic_proc_file_fd_description(flags, mode, path, proc_file);
    entry->fd_flags = (flags & O_CLOEXEC) ? FD_CLOEXEC : 0;
    fdtable_sync_task_file_locked(fd, entry);
    fs_mutex_unlock(&entry->lock);
}

void init_synthetic_cgroupfs_file_fd_entry_impl(int fd, int flags, uint32_t mode,
                                                const char *path, const char *cgroup_path,
                                                int cgroup_node) {
    file_init_impl();
    fd_entry_t *entry = &fd_table[fd];
    fs_mutex_lock(&entry->lock);
    entry->desc = alloc_synthetic_proc_file_fd_description(flags, mode, path, SYNTHETIC_PROC_FILE_NONE);
    if (entry->desc && cgroup_path) {
        strncpy(entry->desc->cgroupfs_path, cgroup_path, MAX_PATH - 1);
        entry->desc->cgroupfs_path[MAX_PATH - 1] = '\0';
        entry->desc->cgroupfs_node = cgroup_node;
    }
    entry->fd_flags = (flags & O_CLOEXEC) ? FD_CLOEXEC : 0;
    fdtable_sync_task_file_locked(fd, entry);
    fs_mutex_unlock(&entry->lock);
}

void init_synthetic_proc_file_fd_entry_for_pid_impl(int fd, int flags, uint32_t mode, const char *path, synthetic_proc_file_t proc_file, int target_pid) {
    file_init_impl();
    fd_entry_t *entry = &fd_table[fd];
    fs_mutex_lock(&entry->lock);
    entry->desc = alloc_synthetic_proc_file_fd_description(flags, mode, path, proc_file);
    if (entry->desc) {
        entry->desc->proc_file_target_pid = target_pid;
    }
    entry->fd_flags = (flags & O_CLOEXEC) ? FD_CLOEXEC : 0;
    fdtable_sync_task_file_locked(fd, entry);
    fs_mutex_unlock(&entry->lock);
}

void init_synthetic_proc_file_fd_entry_with_fdnum_impl(int fd, int flags, uint32_t mode, const char *path, synthetic_proc_file_t proc_file, int fd_num) {
    file_init_impl();
    fd_entry_t *entry = &fd_table[fd];
    fs_mutex_lock(&entry->lock);
    entry->desc = alloc_synthetic_proc_file_fd_description(flags, mode, path, proc_file);
    if (entry->desc) {
        entry->desc->proc_file_fd_num = fd_num;
    }
    entry->fd_flags = (flags & O_CLOEXEC) ? FD_CLOEXEC : 0;
    fdtable_sync_task_file_locked(fd, entry);
    fs_mutex_unlock(&entry->lock);
}

void init_synthetic_proc_file_fd_entry_with_fdnum_for_pid_impl(int fd, int flags, uint32_t mode, const char *path, synthetic_proc_file_t proc_file, int fd_num, int target_pid) {
    file_init_impl();
    fd_entry_t *entry = &fd_table[fd];
    fs_mutex_lock(&entry->lock);
    entry->desc = alloc_synthetic_proc_file_fd_description(flags, mode, path, proc_file);
    if (entry->desc) {
        entry->desc->proc_file_fd_num = fd_num;
        entry->desc->proc_file_target_pid = target_pid;
    }
    entry->fd_flags = (flags & O_CLOEXEC) ? FD_CLOEXEC : 0;
    fdtable_sync_task_file_locked(fd, entry);
    fs_mutex_unlock(&entry->lock);
}

bool get_fd_is_synthetic_proc_file_impl(void *entry) {
    fd_entry_t *fd_entry = (fd_entry_t *)entry;
    return fd_entry->desc && fd_entry->desc->type == FD_TYPE_SYNTHETIC_PROC_FILE;
}

synthetic_proc_file_t get_fd_synthetic_proc_file_impl(void *entry) {
    fd_entry_t *fd_entry = (fd_entry_t *)entry;
    return fd_entry->desc ? fd_entry->desc->proc_file : SYNTHETIC_PROC_FILE_NONE;
}

int get_fd_proc_file_fd_num_impl(void *entry) {
    fd_entry_t *fd_entry = (fd_entry_t *)entry;
    return fd_entry->desc ? fd_entry->desc->proc_file_fd_num : -1;
}

int get_fd_proc_file_target_pid_impl(void *entry) {
    fd_entry_t *fd_entry = (fd_entry_t *)entry;
    return fd_entry->desc ? fd_entry->desc->proc_file_target_pid : -1;
}

bool get_fd_has_cgroupfs_path_impl(void *entry) {
    fd_entry_t *fd_entry = (fd_entry_t *)entry;
    return fd_entry->desc && fd_entry->desc->cgroupfs_path[0] == '/';
}

int get_fd_cgroupfs_path_impl(void *entry, char *path, size_t path_len) {
    fd_entry_t *fd_entry = (fd_entry_t *)entry;
    size_t len;

    if (!fd_entry->desc || !path || path_len == 0 || fd_entry->desc->cgroupfs_path[0] != '/') {
        return -EINVAL;
    }
    len = strlen(fd_entry->desc->cgroupfs_path);
    if (len >= path_len) {
        return -ENAMETOOLONG;
    }
    memcpy(path, fd_entry->desc->cgroupfs_path, len + 1);
    return 0;
}

int get_fd_cgroupfs_node_impl(void *entry) {
    fd_entry_t *fd_entry = (fd_entry_t *)entry;
    return fd_entry->desc ? fd_entry->desc->cgroupfs_node : 0;
}

void init_synthetic_subdir_fd_entry_impl(int fd, int flags, uint32_t mode, const char *path, synthetic_dir_class_t dir_class) {
    file_init_impl();
    fd_entry_t *entry = &fd_table[fd];
    fs_mutex_lock(&entry->lock);
    entry->desc = alloc_synthetic_subdir_fd_description(flags, mode, path, dir_class);
    entry->fd_flags = (flags & O_CLOEXEC) ? FD_CLOEXEC : 0;
    fdtable_sync_task_file_locked(fd, entry);
    fs_mutex_unlock(&entry->lock);
}

synthetic_dir_class_t get_fd_synthetic_dir_class_impl(void *entry) {
    fd_entry_t *fd_entry = (fd_entry_t *)entry;
    if (!fd_entry->desc || fd_entry->desc->type != FD_TYPE_SYNTHETIC_DIR || !fd_entry->desc->synthetic_state) {
        return SYNTHETIC_DIR_GENERIC;
    }
    return ((synthetic_dir_state_t *)fd_entry->desc->synthetic_state)->dir_class;
}

bool fdtable_is_used_impl(int fd) {
    if (fd < 0 || fd >= NR_OPEN_DEFAULT) {
        return false;
    }
    file_init_impl();
    fs_mutex_lock(&fd_table_lock);
    bool used = fd_table[fd].used;
    fs_mutex_unlock(&fd_table_lock);
    return used;
}

static bool fdtable_path_matches_tree(const char *path, const char *root) {
    size_t root_len;

    if (!path || !root || root[0] == '\0') {
        return false;
    }
    if (strcmp(root, "/") == 0) {
        return path[0] == '/';
    }
    root_len = strlen(root);
    return strcmp(path, root) == 0 || (strncmp(path, root, root_len) == 0 && path[root_len] == '/');
}

bool fdtable_has_open_path_under_impl(const char *root) {
    return fdtable_has_open_path_under_mount_namespace_impl(0, root);
}

bool fdtable_has_open_path_under_mount_namespace_impl(uint64_t mount_ns_id, const char *root) {
    bool found = false;

    if (!root) {
        return false;
    }
    file_init_impl();
    fs_mutex_lock(&fd_table_lock);
    for (int fd = 0; fd < NR_OPEN_DEFAULT; fd++) {
        fd_description_t *desc = fd_table[fd].desc;

        if (fd_table[fd].used && desc &&
            (mount_ns_id == 0 || desc->mount_ns_id == 0 || desc->mount_ns_id == mount_ns_id) &&
            fdtable_path_matches_tree(desc->path, root)) {
            found = true;
            break;
        }
    }
    fs_mutex_unlock(&fd_table_lock);
    return found;
}

void fdtable_mark_path_deleted_impl(const char *path) {
    if (!path) {
        return;
    }
    file_init_impl();
    fs_mutex_lock(&fd_table_lock);
    for (int fd = 0; fd < NR_OPEN_DEFAULT; fd++) {
        if (fd_table[fd].used) {
            fdtable_mark_desc_deleted_if_path_matches(fd_table[fd].desc, path);
        }
    }
    fs_mutex_unlock(&fd_table_lock);

    kernel_mutex_lock(&task_table_lock);
    for (int i = 0; i < TASK_MAX_TASKS; i++) {
        struct task *task = task_table[i];
        while (task) {
            if (task->files) {
                fs_mutex_lock(&task->files->lock);
                for (size_t fd = 0; fd < task->files->max_fds; fd++) {
                    struct fd_file *file = task->files->fd[fd];
                    if (file) {
                        fdtable_mark_desc_deleted_if_path_matches((fd_description_t *)file->private_data, path);
                    }
                }
                fs_mutex_unlock(&task->files->lock);
            }
            task = task->hash_next;
        }
    }
    kernel_mutex_unlock(&task_table_lock);
}

void fdtable_rename_path_impl(const char *old_path, const char *new_path) {
    if (!old_path || !new_path) {
        return;
    }
    file_init_impl();
    fs_mutex_lock(&fd_table_lock);
    for (int fd = 0; fd < NR_OPEN_DEFAULT; fd++) {
        if (fd_table[fd].used) {
            fdtable_rename_desc_path_if_matches(fd_table[fd].desc, old_path, new_path);
        }
    }
    fs_mutex_unlock(&fd_table_lock);

    kernel_mutex_lock(&task_table_lock);
    for (int i = 0; i < TASK_MAX_TASKS; i++) {
        struct task *task = task_table[i];
        while (task) {
            if (task->files) {
                fs_mutex_lock(&task->files->lock);
                for (size_t fd = 0; fd < task->files->max_fds; fd++) {
                    struct fd_file *file = task->files->fd[fd];
                    if (file) {
                        fdtable_rename_desc_path_if_matches((fd_description_t *)file->private_data,
                                                            old_path, new_path);
                        if (strcmp(file->path, old_path) == 0) {
                            strncpy(file->path, new_path, sizeof(file->path) - 1);
                            file->path[sizeof(file->path) - 1] = '\0';
                        }
                    }
                }
                fs_mutex_unlock(&task->files->lock);
            }
            task = task->hash_next;
        }
    }
    kernel_mutex_unlock(&task_table_lock);
}

void fdtable_exchange_paths_impl(const char *left_path, const char *right_path) {
    if (!left_path || !right_path) {
        return;
    }
    file_init_impl();
    fs_mutex_lock(&fd_table_lock);
    for (int fd = 0; fd < NR_OPEN_DEFAULT; fd++) {
        if (fd_table[fd].used) {
            fdtable_exchange_desc_path_if_matches(fd_table[fd].desc, left_path, right_path);
        }
    }
    fs_mutex_unlock(&fd_table_lock);

    kernel_mutex_lock(&task_table_lock);
    for (int i = 0; i < TASK_MAX_TASKS; i++) {
        struct task *task = task_table[i];
        while (task) {
            if (task->files) {
                fs_mutex_lock(&task->files->lock);
                for (size_t fd = 0; fd < task->files->max_fds; fd++) {
                    struct fd_file *file = task->files->fd[fd];
                    if (file) {
                        fdtable_exchange_desc_path_if_matches((fd_description_t *)file->private_data,
                                                              left_path, right_path);
                        if (strcmp(file->path, left_path) == 0) {
                            strncpy(file->path, right_path, sizeof(file->path) - 1);
                            file->path[sizeof(file->path) - 1] = '\0';
                        } else if (strcmp(file->path, right_path) == 0) {
                            strncpy(file->path, left_path, sizeof(file->path) - 1);
                            file->path[sizeof(file->path) - 1] = '\0';
                        }
                    }
                }
                fs_mutex_unlock(&task->files->lock);
            }
            task = task->hash_next;
        }
    }
    kernel_mutex_unlock(&task_table_lock);
}

bool fdtable_task_is_used_impl(struct task *task, int fd) {
    bool used;

    if (!task || !task->files || fd < 0 || (size_t)fd >= task->files->max_fds) {
        return false;
    }

    fs_mutex_lock(&task->files->lock);
    used = task->files->fd[fd] != NULL;
    fs_mutex_unlock(&task->files->lock);
    return used;
}

int fdtable_task_fd_path_impl(struct task *task, int fd, char *path, size_t path_len) {
    struct fd_file *file;
    fd_description_t *desc;
    size_t len;
    size_t suffix_len = 0;
    const char deleted_suffix[] = " (deleted)";

    if (!path || path_len == 0) {
        return -EINVAL;
    }
    if (!task || !task->files || fd < 0 || (size_t)fd >= task->files->max_fds) {
        return -EBADF;
    }

    fs_mutex_lock(&task->files->lock);
    file = task->files->fd[fd];
    desc = file ? (fd_description_t *)file->private_data : NULL;
    if (!file || file->path[0] == '\0') {
        fs_mutex_unlock(&task->files->lock);
        return -EBADF;
    }

    len = strlen(file->path);
    if (desc && desc->path_deleted) {
        suffix_len = sizeof(deleted_suffix) - 1;
    }
    if (len + suffix_len >= path_len) {
        fs_mutex_unlock(&task->files->lock);
        return -ENAMETOOLONG;
    }
    memcpy(path, file->path, len + 1);
    if (suffix_len != 0) {
        memcpy(path + len, deleted_suffix, suffix_len + 1);
    }
    fs_mutex_unlock(&task->files->lock);
    return 0;
}

int fdtable_task_fdinfo_content_impl(struct task *task, int fd, unsigned long long mnt_id,
                                     char *buf, size_t buf_len) {
    struct fd_file *file;
    int64_t pos;
    unsigned int flags;
    unsigned int fd_flags;
    int ret;

    if (!buf || buf_len == 0) {
        return -EINVAL;
    }
    if (!task || !task->files || fd < 0 || (size_t)fd >= task->files->max_fds) {
        return -EBADF;
    }

    fs_mutex_lock(&task->files->lock);
    file = task->files->fd[fd];
    if (!file) {
        fs_mutex_unlock(&task->files->lock);
        return -EBADF;
    }
    fd_description_t *desc = (fd_description_t *)file->private_data;
    pos = desc ? (int64_t)desc->offset : file->pos;
    flags = desc ? (unsigned int)desc->flags : file->flags;
    fd_flags = file->fd_flags;
    fs_mutex_unlock(&task->files->lock);

    if (fd_flags & FD_CLOEXEC) {
        flags |= O_CLOEXEC;
    }

    ret = scnprintf(buf, buf_len, "pos:\t%lld\nflags:\t0%o\nmnt_id:\t%llu\n",
                   (long long)pos, flags, mnt_id);
    if (ret < 0) {
        return -EINVAL;
    }
    if ((size_t)ret >= buf_len) {
        return (int)(buf_len - 1);
    }
    return ret;
}

void fdtable_sync_current_task_fd_impl(int fd) {
    struct task *task = task_current();
    struct fd_file *file;
    fd_description_t *desc;

    if (fd < 0 || fd >= NR_OPEN_DEFAULT) {
        return;
    }

    if (task && task->files && (size_t)fd < task->files->max_fds) {
        fs_mutex_lock(&task->files->lock);
        file = task->files->fd[fd];
        desc = file ? (fd_description_t *)file->private_data : NULL;
        if (file && desc) {
            file->real_fd = desc->fd;
            file->flags = (unsigned int)desc->flags;
            file->pos = (int64_t)desc->offset;
            strncpy(file->path, desc->path, sizeof(file->path) - 1);
            file->path[sizeof(file->path) - 1] = '\0';
        }
        fs_mutex_unlock(&task->files->lock);
        return;
    }

    file_init_impl();
    fs_mutex_lock(&fd_table_lock);
    if (fd_table[fd].used) {
        fdtable_sync_task_file_locked(fd, &fd_table[fd]);
    }
    fs_mutex_unlock(&fd_table_lock);
}

void fdtable_sync_current_task_from_static_impl(void) {
    struct task *task = task_current();

    if (!task || !task->files) {
        return;
    }

    file_init_impl();
    fs_mutex_lock(&fd_table_lock);
    for (int fd = 0; fd < NR_OPEN_DEFAULT; fd++) {
        if (fd_table[fd].used) {
            fdtable_sync_task_file_locked(fd, &fd_table[fd]);
        }
    }
    fs_mutex_unlock(&fd_table_lock);
}

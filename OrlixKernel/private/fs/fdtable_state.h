#ifndef PRIVATE_FS_FDTABLE_STATE_H
#define PRIVATE_FS_FDTABLE_STATE_H

#include "fs/fdtable.h"
#include "private/fs/lock_state.h"

#ifdef __cplusplus
extern "C" {
#endif

struct fd_file {
    int fd;
    int real_fd;
    unsigned int flags;
    unsigned int fd_flags;
    int64_t pos;
    char path[MAX_PATH];
    void *private_data;
    atomic_t refs;
};

struct fd_table {
    struct fd_file **fd;
    size_t max_fds;
    fs_mutex_t lock;
    atomic_t refs;
};

struct vfs_mount_fd_entry {
    char source[MAX_PATH];
    char target[MAX_PATH];
    char fstype[32];
    unsigned long flags;
    unsigned long propagation;
};

struct vfs_mount_fd {
    size_t entry_count;
    struct vfs_mount_fd_entry entries[VFS_MOUNT_FD_MAX_ENTRIES];
};

struct fd_entry {
    fd_description_t *desc;
    int fd_flags;
    bool used;
    bool task_local;
    int task_fd;
    fs_mutex_t lock;
};

bool fdtable_task_is_used_impl(struct task *task, int fd);
int fdtable_task_fd_path_impl(struct task *task, int fd, char *path, size_t path_len);
int fdtable_task_fdinfo_content_impl(struct task *task, int fd, unsigned long long mnt_id,
                                     char *buf, size_t buf_len);
void fdtable_sync_current_task_fd_impl(int fd);
void fdtable_sync_current_task_from_static_impl(void);
struct task *pidfd_get_task_entry_impl(void *entry);

#ifdef __cplusplus
}
#endif

#endif

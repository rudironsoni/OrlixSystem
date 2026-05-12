#ifndef FDTABLE_H
#define FDTABLE_H

#include <linux/types.h>
#include <linux/atomic.h>

#include "path.h"
#include "internal/fs/lock.h"

#define NR_OPEN_DEFAULT 256

#ifdef __cplusplus
extern "C" {
#endif

struct fd_file;
struct fd_table;
struct task;
struct __kernel_itimerspec;

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

struct fd_table *alloc_files(size_t max_fds);
void free_files(struct fd_table *files);
struct fd_table *get_files(struct fd_table *files);
struct fd_table *dup_files(struct fd_table *parent);

struct fd_file *alloc_file(void);
void free_file(struct fd_file *file);
struct fd_file *dup_file(struct fd_file *file);

int alloc_fd(struct fd_table *files, struct fd_file *file);
int free_fd(struct fd_table *files, int fd);

/* ============================================================================
 * STATIC FD TABLE API (for host-mediated FDs)
 * These functions work with the internal static fd table, not fd_table
 * ============================================================================ */

/* Initialize/reset the static fd table */
void file_init_impl(void);
void file_deinit_impl(void);

/* Allocate/free slots in static fd table */
int alloc_fd_impl(void);
void free_fd_impl(int fd);

struct fd_description;
typedef struct fd_description fd_description_t;
struct pipe_endpoint;
struct epoll_instance;
struct socket_state;

#define VFS_MOUNT_FD_MAX_ENTRIES 64

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
typedef struct fd_entry fd_entry_t;
/* FD entry access - returns locked entry, must call put_fd_entry_impl to unlock */
fd_entry_t *get_fd_entry_impl(int fd);
void put_fd_entry_impl(void *entry);

/* Getters/setters for fd entry properties */
int get_real_fd_impl(void *entry);
int get_fd_flags_impl(void *entry);
int get_fd_descriptor_flags_impl(void *entry);
bool get_fd_is_synthetic_dir_impl(void *entry);
bool get_fd_is_dir_impl(void *entry);
int get_fd_path_impl(fd_entry_t *entry, char *path, size_t path_len);
uint64_t get_fd_file_identity_impl(void *entry);
bool get_fd_path_deleted_impl(void *entry);
void set_fd_flags_impl(fd_entry_t *entry, int flags);
void set_fd_descriptor_flags_impl(fd_entry_t *entry, int flags);
int64_t get_fd_offset_impl(fd_entry_t *entry);
void set_fd_offset_impl(fd_entry_t *entry, int64_t offset);
bool get_fd_is_append_impl(fd_entry_t *entry);
bool get_fd_is_readable_impl(void *entry);
bool get_fd_is_writable_impl(void *entry);
int clone_fd_entry_impl(int oldfd, int minfd, bool cloexec);
int replace_fd_entry_impl(int newfd, int oldfd, bool cloexec);

/* Initialize/clone fd entries */
void init_fd_entry_impl(int fd, int real_fd, int flags, uint32_t mode, const char *path);
void init_fd_entry_with_identity_impl(int fd, int real_fd, int flags, uint32_t mode,
                                      const char *path, uint64_t file_identity);
void init_backing_dirfd_entry_impl(int fd, int real_fd, uint32_t mode, const char *path);

enum synthetic_dir_class {
    SYNTHETIC_DIR_GENERIC = 0,
    SYNTHETIC_DIR_ROOT,
    SYNTHETIC_DIR_PROC,
    SYNTHETIC_DIR_DEV,
    SYNTHETIC_DIR_DEV_PTS,
    SYNTHETIC_DIR_PROC_SELF,
    SYNTHETIC_DIR_PROC_SELF_FD,
    SYNTHETIC_DIR_PROC_SELF_FDINFO,
    SYNTHETIC_DIR_PROC_SELF_TASK,
    SYNTHETIC_DIR_PROC_SELF_NS,
    SYNTHETIC_DIR_CGROUPFS
};

typedef enum synthetic_dir_class synthetic_dir_class_t;

void init_synthetic_fd_entry_impl(int fd, int flags, uint32_t mode, const char *path);
void init_synthetic_subdir_fd_entry_impl(int fd, int flags, uint32_t mode, const char *path, synthetic_dir_class_t dir_class);

synthetic_dir_class_t get_fd_synthetic_dir_class_impl(void *entry);

enum synthetic_dev_node {
    SYNTHETIC_DEV_NONE = 0,
    SYNTHETIC_DEV_NULL,
    SYNTHETIC_DEV_ZERO,
    SYNTHETIC_DEV_RANDOM,
    SYNTHETIC_DEV_URANDOM,
    SYNTHETIC_DEV_PTMX
};

typedef enum synthetic_dev_node synthetic_dev_node_t;

void init_synthetic_dev_fd_entry_impl(int fd, int flags, uint32_t mode, const char *path, synthetic_dev_node_t dev_node);
void init_synthetic_pty_fd_entry_impl(int fd, int flags, uint32_t mode, const char *path, unsigned int pty_index, bool is_master);
int init_pipe_fd_entry_impl(int fd, int flags, struct pipe_endpoint *endpoint);
int init_socket_fd_entry_impl(int fd, int flags, struct socket_state *socket);
int init_epoll_fd_entry_impl(int fd, int flags, struct epoll_instance *instance);
int init_mount_fd_entry_impl(int fd, int flags, const struct vfs_mount_fd *mount_fd);
int eventfd2_impl(unsigned int initval, int flags);
int timerfd_create_impl(int clockid, int flags);
int timerfd_settime_impl(int fd, int flags, const struct __kernel_itimerspec *new_value,
                         struct __kernel_itimerspec *old_value);
int timerfd_gettime_impl(int fd, struct __kernel_itimerspec *curr_value);
int memfd_create_impl(const char *name, unsigned int flags);
int pidfd_open_impl(int32_t pid, unsigned int flags);
int pidfd_create_for_task_impl(struct task *task, int flags);

bool get_fd_is_synthetic_dev_impl(void *entry);
synthetic_dev_node_t get_fd_synthetic_dev_node_impl(void *entry);
bool get_fd_is_synthetic_pty_impl(void *entry);
bool get_fd_is_synthetic_pty_master_impl(void *entry);
unsigned int get_fd_synthetic_pty_index_impl(void *entry);
bool get_fd_is_pipe_impl(void *entry);
struct pipe_endpoint *get_fd_pipe_endpoint_impl(void *entry);
bool get_fd_is_socket_impl(void *entry);
struct socket_state *get_fd_socket_impl(void *entry);
bool get_fd_is_epoll_impl(void *entry);
struct epoll_instance *get_fd_epoll_instance_impl(void *entry);
bool get_fd_is_mount_impl(void *entry);
int get_fd_mount_impl(void *entry, struct vfs_mount_fd *mount_fd);
bool get_fd_is_eventfd_impl(void *entry);
long eventfd_read_entry_impl(void *entry, void *buf, size_t count);
long eventfd_write_entry_impl(void *entry, const void *buf, size_t count);
bool eventfd_read_ready_entry_impl(void *entry);
bool eventfd_write_ready_entry_impl(void *entry);
bool get_fd_is_timerfd_impl(void *entry);
long timerfd_read_entry_impl(void *entry, void *buf, size_t count);
long timerfd_write_entry_impl(void *entry, const void *buf, size_t count);
bool timerfd_read_ready_entry_impl(void *entry);
bool get_fd_is_memfd_impl(void *entry);
int memfd_get_seals_entry_impl(void *entry);
int memfd_add_seals_entry_impl(void *entry, int seals);
int memfd_write_allowed_entry_impl(void *entry, int64_t offset, size_t count);
int memfd_truncate_allowed_entry_impl(void *entry, int64_t length);
bool get_fd_is_pidfd_impl(void *entry);
struct task *pidfd_get_task_entry_impl(void *entry);
bool pidfd_read_ready_entry_impl(void *entry);

enum synthetic_proc_file {
    SYNTHETIC_PROC_FILE_NONE = 0,
    SYNTHETIC_PROC_FILE_CMDLINE,
    SYNTHETIC_PROC_FILE_ENVIRON,
    SYNTHETIC_PROC_FILE_COMM,
    SYNTHETIC_PROC_FILE_STAT,
    SYNTHETIC_PROC_FILE_STATM,
    SYNTHETIC_PROC_FILE_MAPS,
    SYNTHETIC_PROC_FILE_SMAPS,
    SYNTHETIC_PROC_FILE_STATUS,
    SYNTHETIC_PROC_FILE_CGROUP,
    SYNTHETIC_PROC_FILE_UID_MAP,
    SYNTHETIC_PROC_FILE_GID_MAP,
    SYNTHETIC_PROC_FILE_SETGROUPS,
    SYNTHETIC_PROC_FILE_MOUNTINFO,
    SYNTHETIC_PROC_FILE_MOUNTS,
    SYNTHETIC_PROC_FILE_FDINFO,
    SYNTHETIC_PROC_FILE_FILESYSTEMS,
    SYNTHETIC_PROC_FILE_MEMINFO,
    SYNTHETIC_PROC_FILE_CPUINFO
};

typedef enum synthetic_proc_file synthetic_proc_file_t;

void init_synthetic_proc_file_fd_entry_impl(int fd, int flags, uint32_t mode, const char *path, synthetic_proc_file_t proc_file);
void init_synthetic_cgroupfs_file_fd_entry_impl(int fd, int flags, uint32_t mode,
                                                const char *path, const char *cgroup_path,
                                                int cgroup_node);
void init_synthetic_proc_file_fd_entry_for_pid_impl(int fd, int flags, uint32_t mode, const char *path, synthetic_proc_file_t proc_file, int target_pid);
void init_synthetic_proc_file_fd_entry_with_fdnum_impl(int fd, int flags, uint32_t mode, const char *path, synthetic_proc_file_t proc_file, int fd_num);
void init_synthetic_proc_file_fd_entry_with_fdnum_for_pid_impl(int fd, int flags, uint32_t mode, const char *path, synthetic_proc_file_t proc_file, int fd_num, int target_pid);
bool get_fd_is_synthetic_proc_file_impl(void *entry);
synthetic_proc_file_t get_fd_synthetic_proc_file_impl(void *entry);
int get_fd_proc_file_fd_num_impl(void *entry);
int get_fd_proc_file_target_pid_impl(void *entry);
bool get_fd_has_cgroupfs_path_impl(void *entry);
int get_fd_cgroupfs_path_impl(void *entry, char *path, size_t path_len);
int get_fd_cgroupfs_node_impl(void *entry);

bool fdtable_is_used_impl(int fd);
bool fdtable_has_open_path_under_impl(const char *root);
bool fdtable_has_open_path_under_mount_namespace_impl(uint64_t mount_ns_id, const char *root);
void fdtable_mark_path_deleted_impl(const char *path);
void fdtable_rename_path_impl(const char *old_path, const char *new_path);
void fdtable_exchange_paths_impl(const char *left_path, const char *right_path);
bool fdtable_task_is_used_impl(struct task *task, int fd);
int fdtable_task_fd_path_impl(struct task *task, int fd, char *path, size_t path_len);
int fdtable_task_fdinfo_content_impl(struct task *task, int fd, unsigned long long mnt_id,
                                     char *buf, size_t buf_len);
void fdtable_sync_current_task_fd_impl(int fd);
void fdtable_sync_current_task_from_static_impl(void);

/* Close implementation using static fd table */
int close_impl(int fd);
int close_range_impl(unsigned int first, unsigned int last, unsigned int flags);
int close_on_exec_impl(void);

#ifdef __cplusplus
}
#endif

#endif

#ifndef VFS_H
#define VFS_H

#include <asm/statfs.h>
#include <linux/atomic.h>
#include <linux/types.h>

#include "fdtable.h"
#include "internal/fs/lock.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_MOUNTS 64

/* VFS backing storage classes */
enum vfs_backing_class {
    VFS_BACKING_PERSISTENT = 0,
    VFS_BACKING_CACHE,
    VFS_BACKING_TEMP,
    VFS_BACKING_SYNTHETIC,
    VFS_BACKING_EXTERNAL,

    VFS_BACKING_CLASS_COUNT
};

/* Linux-visible route identities for VFS policy decisions */
enum vfs_route_identity {
    VFS_ROUTE_PERSISTENT_ROOT = 0,
    VFS_ROUTE_ETC,
    VFS_ROUTE_USR,
    VFS_ROUTE_VAR_LIB,
    VFS_ROUTE_HOME,
    VFS_ROUTE_ROOT_HOME,
    VFS_ROUTE_VAR_CACHE,
    VFS_ROUTE_TMP,
    VFS_ROUTE_VAR_TMP,
    VFS_ROUTE_RUN,
    VFS_ROUTE_PROC,
    VFS_ROUTE_SYS,
    VFS_ROUTE_DEV,

    VFS_ROUTE_IDENTITY_COUNT
};

/* Linux VFS inode types (file types)
 * These are defined in standard system headers, no need to redefine */

/* Forward declarations */
struct vfs_inode;
struct vfs_dentry;
struct fd_file;
struct stat;
struct timespec;
struct super_block;
struct vfs_file_system;
struct vfs_mount;
struct vfs_mount_namespace;
struct mount_attr;
struct mnt_id_req;
struct statmount;
struct poll_table_struct;
struct iattr;
struct page;
struct address_space;
struct writeback_control;

/* Linux-compatible VFS operations structure */
struct vfs_file_ops {
    int64_t (*read)(struct fd_file *file, char *buf, size_t count, int64_t *pos);
    int64_t (*write)(struct fd_file *file, const char *buf, size_t count, int64_t *pos);
    int (*open)(struct vfs_inode *inode, struct fd_file *file);
    int (*release)(struct vfs_inode *inode, struct fd_file *file);
    int (*ioctl)(struct fd_file *file, unsigned int cmd, unsigned long arg);
    int (*mmap)(struct fd_file *file, void *addr, size_t len, int prot, int flags, int64_t offset);
    unsigned int (*poll)(struct fd_file *file, struct poll_table_struct *table);
};

/* Linux-compatible inode operations */
struct vfs_inode_ops {
    struct vfs_dentry *(*lookup)(struct vfs_inode *dir, struct vfs_dentry *dentry);
    int (*create)(struct vfs_inode *dir, struct vfs_dentry *dentry, uint32_t mode);
    int (*link)(struct vfs_dentry *old_dentry, struct vfs_inode *dir, struct vfs_dentry *new_dentry);
    int (*unlink)(struct vfs_inode *dir, struct vfs_dentry *dentry);
    int (*symlink)(struct vfs_inode *dir, struct vfs_dentry *dentry, const char *oldname);
    int (*mkdir)(struct vfs_inode *dir, struct vfs_dentry *dentry, uint32_t mode);
    int (*rmdir)(struct vfs_inode *dir, struct vfs_dentry *dentry);
    int (*rename)(struct vfs_inode *old_dir, struct vfs_dentry *old_dentry, struct vfs_inode *new_dir,
                  struct vfs_dentry *new_dentry);
    int (*readlink)(struct vfs_dentry *dentry, char *buf, int buflen);
    int (*setattr)(struct vfs_dentry *dentry, struct iattr *attr);
    int (*getattr)(const char *path, struct vfs_dentry *dentry, struct stat *statbuf);
};

/* Linux-compatible address space operations */
struct vfs_mapping_ops {
    int (*readpage)(struct fd_file *file, struct page *page);
    int (*writepage)(struct page *page, struct writeback_control *wbc);
    int (*write_begin)(struct fd_file *file, struct address_space *mapping, int64_t pos, unsigned len,
                       unsigned flags, struct page **pagep, void **fsdata);
    int (*write_end)(struct fd_file *file, struct address_space *mapping, int64_t pos, unsigned len,
                     unsigned copied, struct page *page, void *fsdata);
};

/* Linux-compatible super block operations */
struct vfs_super_ops {
    struct vfs_inode *(*alloc_inode)(struct super_block *sb);
    void (*destroy_inode)(struct vfs_inode *inode);
    void (*dirty_inode)(struct vfs_inode *inode);
    int (*write_inode)(struct vfs_inode *inode, struct writeback_control *wbc);
    void (*evict_inode)(struct vfs_inode *inode);
    int (*statfs)(struct vfs_dentry *dentry, struct statfs *buf);
    int (*remount_fs)(struct super_block *sb, int *flags, char *data);
    void (*clear_inode)(struct vfs_inode *inode);
    void (*umount_begin)(struct super_block *sb);
};

/* Linux-compatible inode structure */
struct vfs_inode {
    uint64_t i_ino;
    unsigned int i_mode;
    uint32_t i_uid;
    uint32_t i_gid;
    int64_t i_size;
    struct timespec *i_atime;
    struct timespec *i_mtime;
    struct timespec *i_ctime;
    atomic_t i_count;
    void *i_private;
    struct super_block *i_sb;
    const struct vfs_inode_ops *i_op;
    const struct vfs_file_ops *i_fop;
    void *i_fspriv;
};

/* Linux-compatible dentry (directory entry) structure */
struct vfs_dentry {
    struct vfs_inode *d_inode;
    struct super_block *d_sb;
    const unsigned char *d_name;
    atomic_t d_count;
    struct vfs_dentry *d_parent;
    void *d_fsdata;
};

/* Linux-compatible file system type */
struct vfs_file_system {
    const char *name;
    int (*mount)(struct vfs_file_system *fs_type, int flags, const char *dev_name, void *data,
                  struct vfs_dentry *mnt_root);
    void (*kill_sb)(struct super_block *sb);
    struct module *owner;
};

/* Linux-compatible mount point */
struct vfs_mount {
    struct vfs_dentry *mnt_root;
    struct super_block *mnt_sb;
    int mnt_flags;
    char mnt_devname[MAX_PATH];
    atomic_t mnt_count;
    atomic_t mnt_ondie;
    struct vfs_mount *mnt_parent;
};

/* Linux-compatible fs context (per-task filesystem context)
 * Stores virtual root and pwd as char arrays for task-aware path resolution */
struct fs_context {
    struct vfs_dentry *root;
    struct vfs_dentry *pwd;
    uint32_t umask;
    atomic_t users;
    fs_mutex_t lock;
    /* Task-aware path resolution state */
    char root_path[MAX_PATH];      /* Virtual root path (absolute, normalized) */
    char pwd_path[MAX_PATH];       /* Virtual pwd path (absolute, normalized) */
    struct vfs_mount_namespace *mnt_ns;
};

/* VFS context API */
struct fs_context *alloc_fs_struct(void);
struct fs_context *get_fs_struct(struct fs_context *fs);
void free_fs_struct(struct fs_context *fs);
struct fs_context *dup_fs_struct(struct fs_context *old);
int fs_init_root(struct fs_context *fs, const char *root_path);
int fs_init_pwd(struct fs_context *fs, const char *pwd_path);
int fs_set_pwd(struct fs_context *fs, const char *new_pwd);
int fs_set_root(struct fs_context *fs, const char *new_root);
int fs_unshare_mount_namespace(struct fs_context *fs);
uint64_t fs_mount_namespace_id(struct fs_context *fs);
unsigned int fs_mount_namespace_refs(struct fs_context *fs);
unsigned int fs_mount_namespace_active_mounts(struct fs_context *fs);
int vfs_apply_mounts_to_path(const char *normalized_virtual_path, char *mounted_path,
                             size_t mounted_path_len);

/* VFS initialization */
int vfs_init(void);
void vfs_deinit(void);

/* Mount operations */
int vfs_mount(const char *source, const char *target, const char *fstype, unsigned long flags,
              const void *data);
int vfs_umount(const char *target);
int vfs_umount_lazy(const char *target);
int vfs_umount_expire(const char *target);
int vfs_umount_force(const char *target);
int vfs_reap_detached_mount_refs(void);
unsigned int vfs_detached_mount_ref_count(void);
unsigned long vfs_mount_flags_for_path(const char *resolved_vpath);
int vfs_mount_setattr(int dirfd, const char *pathname, unsigned int flags,
                      const struct mount_attr *attr, size_t size);
int vfs_open_tree(int dirfd, const char *pathname, unsigned int flags);
int vfs_move_mount(int from_dirfd, const char *from_pathname, int to_dirfd,
                   const char *to_pathname, unsigned int flags);
int vfs_pivot_root(const char *new_root, const char *put_old);
long vfs_listmount(const struct mnt_id_req *req, uint64_t *mnt_ids, size_t nr_mnt_ids,
                   unsigned int flags);
int vfs_statmount(const struct mnt_id_req *req, struct statmount *buf, size_t bufsize,
                  unsigned int flags);
int vfs_mount_basic(void);

/* File operations through VFS */
int vfs_open(const char *path, int flags, uint32_t mode, int *target_fd);

/* Task-aware path translation between virtual and host paths */
int vfs_translate_path(const char *vpath, char *backing_path, size_t backing_path_len);
int vfs_translate_path_task(const char *vpath, char *backing_path, size_t backing_path_len,
                            struct fs_context *fs);
int vfs_translate_path_at(int dirfd, const char *vpath, char *backing_path, size_t backing_path_len);
int vfs_resolve_virtual_path_task(const char *vpath, char *resolved_vpath, size_t resolved_vpath_len,
                                  struct fs_context *fs);
int vfs_resolve_virtual_path_at(int dirfd, const char *vpath, char *resolved_vpath,
                                size_t resolved_vpath_len);
int vfs_resolve_virtual_path_task_follow(const char *vpath, char *resolved_vpath,
                                         size_t resolved_vpath_len, struct fs_context *fs,
                                         int follow_final_symlink);
int vfs_resolve_virtual_path_at_follow(int dirfd, const char *vpath, char *resolved_vpath,
                                       size_t resolved_vpath_len, int follow_final_symlink);
int vfs_getcwd_path_task(struct fs_context *fs, char *vpath, size_t vpath_len);
int vfs_normalize_linux_path(const char *input, char *output, size_t output_len);
int vfs_reverse_translate(const char *backing_path, char *vpath, size_t vpath_len);
const char *vfs_primary_backing_root(void);
const char *vfs_virtual_root(void);

/* Backing class determination for storage policy routing */
enum vfs_backing_class vfs_backing_class_for_path(const char *vpath);
int vfs_describe_route_for_path(const char *vpath, enum vfs_route_identity *route_id,
                                enum vfs_backing_class *backing_class, bool *reversible);
bool vfs_path_is_linux_route(const char *vpath);
bool vfs_path_is_synthetic(const char *vpath);
bool vfs_path_is_synthetic_root(const char *vpath);
synthetic_dev_node_t vfs_path_is_synthetic_dev_node(const char *vpath);

enum proc_self_path_class {
    PROC_SELF_NONE = 0,
    PROC_SELF_DIR,
    PROC_SELF_FD_DIR,
    PROC_SELF_FDINFO_DIR,
    PROC_SELF_TASK_DIR,
    PROC_SELF_THREAD_DIR,
    PROC_SELF_FD_LINK,
    PROC_SELF_CWD_LINK,
    PROC_SELF_EXE_LINK,
    PROC_SELF_CMDLINE_FILE,
    PROC_SELF_ENVIRON_FILE,
    PROC_SELF_COMM_FILE,
    PROC_SELF_STAT_FILE,
    PROC_SELF_STATM_FILE,
    PROC_SELF_MAPS_FILE,
    PROC_SELF_SMAPS_FILE,
    PROC_SELF_FDINFO_FILE,
    PROC_SELF_STATUS_FILE,
    PROC_SELF_CGROUP_FILE,
    PROC_SELF_UID_MAP_FILE,
    PROC_SELF_GID_MAP_FILE,
    PROC_SELF_SETGROUPS_FILE,
    PROC_SELF_MOUNTINFO_FILE,
    PROC_SELF_MOUNTS_FILE,
    PROC_ROOT_FILESYSTEMS_FILE,
    PROC_ROOT_MEMINFO_FILE,
    PROC_ROOT_CPUINFO_FILE,
    PROC_SELF_NS_DIR,
    PROC_SELF_NS_LINK
};

typedef enum proc_self_path_class proc_self_path_class_t;

proc_self_path_class_t vfs_classify_proc_self_path(const char *vpath);
int vfs_proc_target_pid_for_path(const char *vpath);
int vfs_proc_fd_num_for_path(const char *vpath, const char *leaf);
bool vfs_proc_fd_exists_for_path(const char *vpath, int fd_num);
int vfs_proc_self_fd_link_target(const char *vpath, char *target, size_t target_len);
int vfs_proc_task_fd_link_target(int32_t pid, int fd_num, char *target, size_t target_len);
int vfs_proc_self_cwd_target(char *target, size_t target_len);
int vfs_proc_task_cwd_target(int32_t pid, char *target, size_t target_len);
int vfs_proc_self_exe_target(char *target, size_t target_len);
int vfs_proc_task_exe_target(int32_t pid, char *target, size_t target_len);
int vfs_proc_self_ns_link_target(const char *vpath, char *target, size_t target_len);
int vfs_proc_self_cmdline_content(char *buf, size_t buf_len);
int vfs_proc_task_cmdline_content(int32_t pid, char *buf, size_t buf_len);
int vfs_proc_self_environ_content(char *buf, size_t buf_len);
int vfs_proc_task_environ_content(int32_t pid, char *buf, size_t buf_len);
int vfs_proc_self_comm_content(char *buf, size_t buf_len);
int vfs_proc_task_comm_content(int32_t pid, char *buf, size_t buf_len);
int vfs_proc_self_stat_content(char *buf, size_t buf_len);
int vfs_proc_task_stat_content(int32_t pid, char *buf, size_t buf_len);
int vfs_proc_self_statm_content(char *buf, size_t buf_len);
int vfs_proc_task_statm_content(int32_t pid, char *buf, size_t buf_len);
int vfs_proc_self_maps_content(char *buf, size_t buf_len);
int vfs_proc_task_maps_content(int32_t pid, char *buf, size_t buf_len);
int vfs_proc_self_smaps_content(char *buf, size_t buf_len);
int vfs_proc_task_smaps_content(int32_t pid, char *buf, size_t buf_len);
int vfs_proc_self_fdinfo_content(int fd_num, char *buf, size_t buf_len);
int vfs_proc_task_fdinfo_content(int32_t pid, int fd_num, char *buf, size_t buf_len);
int vfs_proc_self_status_content(char *buf, size_t buf_len);
int vfs_proc_task_status_content(int32_t pid, char *buf, size_t buf_len);
int vfs_proc_task_uid_map_content(int32_t pid, char *buf, size_t buf_len);
int vfs_proc_task_gid_map_content(int32_t pid, char *buf, size_t buf_len);
int vfs_proc_task_setgroups_content(int32_t pid, char *buf, size_t buf_len);
long vfs_proc_task_write_id_map_content(synthetic_proc_file_t proc_file, int32_t pid,
                                        const char *buf, size_t count);
int vfs_proc_task_cgroup_content(int32_t pid, char *buf, size_t buf_len);
int vfs_proc_self_mountinfo_content(char *buf, size_t buf_len);
int vfs_proc_task_mountinfo_content(int32_t pid, char *buf, size_t buf_len);
int vfs_proc_self_mounts_content(char *buf, size_t buf_len);
int vfs_proc_task_mounts_content(int32_t pid, char *buf, size_t buf_len);
int vfs_proc_filesystems_content(char *buf, size_t buf_len);
int vfs_proc_meminfo_content(char *buf, size_t buf_len);
int vfs_proc_cpuinfo_content(char *buf, size_t buf_len);
int vfs_check_open_permission(const char *resolved_vpath, const char *translated_path, int flags);
int vfs_check_parent_mutation_permission(const char *resolved_vpath);
int vfs_check_sticky_unlink_permission(const char *resolved_vpath);
int vfs_check_sticky_rename_permission(const char *old_resolved_vpath, const char *new_resolved_vpath);
void vfs_record_created_path(const char *resolved_vpath, uint32_t mode);
uint64_t vfs_file_identity_for_path(const char *resolved_vpath);
int vfs_set_file_capabilities(const char *path, uint64_t permitted, uint64_t inheritable,
                              bool effective);
int vfs_get_file_capabilities(const char *path, uint64_t *permitted, uint64_t *inheritable,
                              bool *effective);
int vfs_remove_file_capabilities(const char *path);
int vfs_set_file_capabilities_follow(const char *path, uint64_t permitted, uint64_t inheritable,
                                     bool effective, int follow_final_symlink);
int vfs_get_file_capabilities_follow(const char *path, uint64_t *permitted,
                                     uint64_t *inheritable, bool *effective,
                                     int follow_final_symlink);
int vfs_remove_file_capabilities_follow(const char *path, int follow_final_symlink);
int vfs_set_user_xattr(const char *path, const char *name, const void *value, size_t size, int flags);
long vfs_get_user_xattr(const char *path, const char *name, void *value, size_t size);
long vfs_list_xattr(const char *path, char *list, size_t size);
int vfs_remove_user_xattr(const char *path, const char *name);
int vfs_set_user_xattr_follow(const char *path, const char *name, const void *value, size_t size,
                              int flags, int follow_final_symlink);
long vfs_get_user_xattr_follow(const char *path, const char *name, void *value, size_t size,
                               int follow_final_symlink);
long vfs_list_xattr_follow(const char *path, char *list, size_t size, int follow_final_symlink);
int vfs_remove_user_xattr_follow(const char *path, const char *name, int follow_final_symlink);
void vfs_forget_path_metadata(const char *resolved_vpath);
void vfs_link_path_metadata(const char *old_resolved_vpath, const char *new_resolved_vpath);
void vfs_rename_path_metadata(const char *old_resolved_vpath, const char *new_resolved_vpath);
void vfs_exchange_path_metadata(const char *left_resolved_vpath, const char *right_resolved_vpath);
void vfs_apply_stat_metadata(const char *resolved_vpath, struct stat *statbuf);
int vfs_chmod_metadata(const char *resolved_vpath, uint32_t mode);
int vfs_chown_metadata(const char *resolved_vpath, uint32_t owner, uint32_t group);
int vfs_utimens_metadata(const char *resolved_vpath, long atime_sec, unsigned long atime_nsec,
                         long mtime_sec, unsigned long mtime_nsec);

const char *vfs_backing_root_for_class(enum vfs_backing_class cls);

/* Backing root accessors for different storage classes */
const char *vfs_persistent_backing_root(void);
const char *vfs_cache_backing_root(void);
const char *vfs_temp_backing_root(void);

/* Stat operations */
int vfs_stat_path(const char *pathname, struct stat *statbuf);
int vfs_lstat(const char *pathname, struct stat *statbuf);
int vfs_path_exists(const char *pathname);
int vfs_access(const char *pathname, int mode);
int vfs_fstatat(int dirfd, const char *pathname, struct stat *statbuf, int flags);
int vfs_faccessat(int dirfd, const char *pathname, int mode, int flags);

#ifdef __cplusplus
}
#endif

#endif

#ifndef PRIVATE_FS_VFS_STATE_H
#define PRIVATE_FS_VFS_STATE_H

#include <asm/statfs.h>
#include <linux/atomic.h>
#include <linux/types.h>

#include "fs/fdtable.h"
#include "private/fs/fdtable_state.h"
#include "fs/vfs.h"
#include "private/fs/lock_state.h"

#ifdef __cplusplus
extern "C" {
#endif

synthetic_dev_node_t vfs_path_is_synthetic_dev_node(const char *vpath);
long vfs_proc_task_write_id_map_content(synthetic_proc_file_t proc_file, int32_t pid,
                                        const char *buf, size_t count);

struct vfs_file_ops {
    int64_t (*read)(struct fd_file *file, char *buf, size_t count, int64_t *pos);
    int64_t (*write)(struct fd_file *file, const char *buf, size_t count, int64_t *pos);
    int (*open)(struct vfs_inode *inode, struct fd_file *file);
    int (*release)(struct vfs_inode *inode, struct fd_file *file);
    int (*ioctl)(struct fd_file *file, unsigned int cmd, unsigned long arg);
    int (*mmap)(struct fd_file *file, void *addr, size_t len, int prot, int flags, int64_t offset);
    unsigned int (*poll)(struct fd_file *file, struct poll_table_struct *table);
};

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

struct vfs_mapping_ops {
    int (*readpage)(struct fd_file *file, struct page *page);
    int (*writepage)(struct page *page, struct writeback_control *wbc);
    int (*write_begin)(struct fd_file *file, struct address_space *mapping, int64_t pos, unsigned len,
                       unsigned flags, struct page **pagep, void **fsdata);
    int (*write_end)(struct fd_file *file, struct address_space *mapping, int64_t pos, unsigned len,
                     unsigned copied, struct page *page, void *fsdata);
};

struct vfs_super_ops {
    struct vfs_inode *(*alloc_inode)(struct vfs_super_block *sb);
    void (*destroy_inode)(struct vfs_inode *inode);
    void (*dirty_inode)(struct vfs_inode *inode);
    int (*write_inode)(struct vfs_inode *inode, struct writeback_control *wbc);
    void (*evict_inode)(struct vfs_inode *inode);
    int (*statfs)(struct vfs_dentry *dentry, struct statfs *buf);
    int (*remount_fs)(struct vfs_super_block *sb, int *flags, char *data);
    void (*clear_inode)(struct vfs_inode *inode);
    void (*umount_begin)(struct vfs_super_block *sb);
};

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
    struct vfs_super_block *i_sb;
    const struct vfs_inode_ops *i_op;
    const struct vfs_file_ops *i_fop;
    void *i_fspriv;
};

struct vfs_dentry {
    struct vfs_inode *d_inode;
    struct vfs_super_block *d_sb;
    const unsigned char *d_name;
    atomic_t d_count;
    struct vfs_dentry *d_parent;
    void *d_fsdata;
};

struct vfs_file_system {
    const char *name;
    int (*mount)(struct vfs_file_system *fs_type, int flags, const char *dev_name, void *data,
                 struct vfs_dentry *mnt_root);
    void (*kill_sb)(struct vfs_super_block *sb);
    void *owner;
};

struct vfs_mount {
    struct vfs_dentry *mnt_root;
    struct vfs_super_block *mnt_sb;
    int mnt_flags;
    char mnt_devname[MAX_PATH];
    atomic_t mnt_count;
    atomic_t mnt_ondie;
    struct vfs_mount *mnt_parent;
};

struct fs_context {
    struct vfs_dentry *root;
    struct vfs_dentry *pwd;
    uint32_t umask;
    atomic_t users;
    fs_mutex_t lock;
    char root_path[MAX_PATH];
    char pwd_path[MAX_PATH];
    struct vfs_mount_namespace *mnt_ns;
};

#ifdef __cplusplus
}
#endif

#endif

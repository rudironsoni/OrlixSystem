#include "vfs.h"
#include "private/fs/vfs_state.h"

/* Linux UAPI headers for ABI constants */
#include <uapi/linux/capability.h>
#include <linux/errno.h>
#include <uapi/linux/elf.h>
#include <uapi/linux/fcntl.h>
#include <linux/mount.h>
#include <uapi/linux/mount.h>
#include <uapi/linux/signal.h>
#include <uapi/asm/stat.h>
#include <uapi/linux/stat.h>
#include <linux/limits.h>
#include <linux/stdarg.h>
#include <linux/string.h>
#include <uapi/linux/xattr.h>
#include "internal/slab.h"

/* Narrow seam headers for host operations */
#include "internal/fs/file.h"
#include "internal/fs/namei.h"
#include "internal/fs/rootfs.h"

#include "fdtable.h"
#include "pty.h"
#include "../private/kernel/task_state.h"
#include "../private/kernel/signal_state.h"
#include "../kernel/cgroup.h"
#include "../kernel/task.h"
#include "../kernel/cred.h"
#include "../kernel/signal.h"
#include "../kernel/uts.h"

/* makedev for device nodes - Linux UAPI style */
#ifndef makedev
#define makedev(major, minor) ((((major) & 0xfff) << 8) | ((minor) & 0xff))
#endif

#define VFS_SYMLINK_MAX 40

static const char *vfs_virtual_root_path = "/";

static int vfs_ensure_backing_initialized(void);
static int vfs_join_virtual_path(const char *base_path, const char *suffix, char *joined_path,
                                 size_t joined_path_len);
extern int epoll_fdinfo_content_impl(struct epoll_instance *instance, char *buf, size_t buf_len,
                                     size_t *pos);
static int vfs_parse_decimal_i32(const char *s, int allow_zero, int32_t *value_out,
                                 const char **end_out);
static int vfs_stat_virtual_backed_path(const char *resolved_vpath, const char *translated_path,
                                        struct stat *st);
static bool vfs_path_is_on_readonly_mount(const char *resolved_vpath);
static int vfs_proc_append(char *buf, size_t buf_len, size_t *pos, const char *fmt, ...);
static int vfs_copy_string(const char *src, char *dst, size_t dst_len);
static bool vfs_path_matches_prefix(const char *vpath, const char *prefix);
static bool vfs_mount_target_matches_tree(const char *target, const char *root);
static uint64_t vfs_vma_resident_page_count(const struct task_vma *vma);
static void vfs_detached_mount_drop_namespace_refs(uint64_t mount_ns_id);

static int vfs_parse_decimal_i32(const char *s, int allow_zero, int32_t *value_out,
                                 const char **end_out) {
    uint64_t value = 0;
    const char *cursor;

    if (!s || *s < '0' || *s > '9' || !value_out || !end_out) {
        return -EINVAL;
    }

    cursor = s;
    while (*cursor >= '0' && *cursor <= '9') {
        value = (value * 10U) + (uint64_t)(*cursor - '0');
        if (value > 2147483647ULL) {
            return -ERANGE;
        }
        cursor++;
    }

    if (!allow_zero && value == 0) {
        return -EINVAL;
    }

    *value_out = (int32_t)value;
    *end_out = cursor;
    return 0;
}

/* Backing storage class roots - discovered at runtime from host container */
static char vfs_persistent_root[MAX_PATH] = {0};
static char vfs_cache_root[MAX_PATH] = {0};
static char vfs_temp_root[MAX_PATH] = {0};
static int vfs_backing_initialized = 0;
static int vfs_etc_bootstrapped = 0;

struct vfs_mount_entry {
    bool active;
    uint64_t mount_id;
    char source[MAX_PATH];
    char target[MAX_PATH];
    char fstype[32];
    unsigned long flags;
    unsigned long propagation;
    uint64_t peer_group_id;
    uint64_t master_group_id;
    bool expiry_candidate;
};

struct vfs_mount_namespace {
    struct vfs_mount_entry entries[MAX_MOUNTS];
    atomic_t refs;
    uint64_t ns_id;
    uint64_t owner_user_ns_id;
    fs_mutex_t lock;
};

#define VFS_METADATA_MAX 256
#define VFS_USER_XATTR_MAX 8
#define VFS_USER_XATTR_NAME_MAX 255
#define VFS_USER_XATTR_VALUE_MAX 1024

struct vfs_user_xattr_entry {
    bool active;
    char name[VFS_USER_XATTR_NAME_MAX + 1];
    size_t size;
    uint8_t value[VFS_USER_XATTR_VALUE_MAX];
};

struct vfs_metadata_entry {
    bool active;
    bool has_attrs;
    char path[MAX_PATH];
    uint64_t file_identity;
    uint32_t uid;
    uint32_t gid;
    uint32_t mode;
    bool has_times;
    long atime_sec;
    unsigned long atime_nsec;
    long mtime_sec;
    unsigned long mtime_nsec;
    bool has_file_caps;
    uint64_t cap_permitted;
    uint64_t cap_inheritable;
    bool cap_effective;
    struct vfs_user_xattr_entry user_xattrs[VFS_USER_XATTR_MAX];
};

static struct vfs_metadata_entry vfs_metadata_table[VFS_METADATA_MAX];
static fs_mutex_t vfs_metadata_lock = FS_MUTEX_INITIALIZER;
static atomic_t vfs_next_mount_ns_id = ATOMIC_INIT(1);
static atomic_t vfs_next_file_identity = ATOMIC_INIT(1);
static atomic_t vfs_next_mount_id = ATOMIC_INIT(2);
static atomic_t vfs_next_mount_peer_group_id = ATOMIC_INIT(1);

#define VFS_DETACHED_MOUNT_MAX 64

struct vfs_detached_mount_ref {
    bool active;
    uint64_t mount_ns_id;
    char target[MAX_PATH];
};

static struct vfs_detached_mount_ref vfs_detached_mount_refs[VFS_DETACHED_MOUNT_MAX];
static fs_mutex_t vfs_detached_mount_lock = FS_MUTEX_INITIALIZER;

static uint64_t vfs_mount_next_peer_group_id(void);

static struct vfs_mount_namespace *vfs_alloc_mount_namespace(void) {
    struct vfs_mount_namespace *mnt_ns =
        __kmalloc_noprof(sizeof(struct vfs_mount_namespace), GFP_KERNEL | __GFP_ZERO);
    if (!mnt_ns) {
        return NULL;
    }

    atomic_set(&mnt_ns->refs, 1);
    mnt_ns->ns_id = (uint64_t)atomic_inc_return(&vfs_next_mount_ns_id);
    mnt_ns->owner_user_ns_id = cred_user_namespace_id(get_current_cred());
    if (mnt_ns->owner_user_ns_id == 0) {
        mnt_ns->owner_user_ns_id = 1;
    }
    fs_mutex_init(&mnt_ns->lock);
    return mnt_ns;
}

static struct vfs_mount_namespace *vfs_get_mount_namespace(struct vfs_mount_namespace *mnt_ns) {
    if (mnt_ns) {
        atomic_inc(&mnt_ns->refs);
    }
    return mnt_ns;
}

static void vfs_put_mount_namespace(struct vfs_mount_namespace *mnt_ns) {
    if (!mnt_ns) {
        return;
    }

    if (atomic_dec_return(&mnt_ns->refs) > 0) {
        return;
    }

    vfs_detached_mount_drop_namespace_refs(mnt_ns->ns_id);
    fs_mutex_lock(&mnt_ns->lock);
    memset(mnt_ns->entries, 0, sizeof(mnt_ns->entries));
    fs_mutex_unlock(&mnt_ns->lock);
    fs_mutex_destroy(&mnt_ns->lock);
    kfree(mnt_ns);
}

static struct vfs_mount_namespace *vfs_dup_mount_namespace(struct vfs_mount_namespace *old) {
    struct vfs_mount_namespace *new_ns;
    struct {
        uint64_t old_id;
        uint64_t new_id;
    } group_map[MAX_MOUNTS];
    size_t group_count = 0;

    if (!old) {
        return vfs_alloc_mount_namespace();
    }

    new_ns = vfs_alloc_mount_namespace();
    if (!new_ns) {
        return NULL;
    }

    fs_mutex_lock(&old->lock);
    memcpy(new_ns->entries, old->entries, sizeof(new_ns->entries));
    new_ns->owner_user_ns_id = cred_user_namespace_id(get_current_cred());
    if (new_ns->owner_user_ns_id == 0) {
        new_ns->owner_user_ns_id = old->owner_user_ns_id;
    }
    for (size_t i = 0; i < MAX_MOUNTS; i++) {
        struct vfs_mount_entry *entry = &new_ns->entries[i];

        if (!entry->active || entry->propagation != MS_SHARED || entry->peer_group_id == 0) {
            continue;
        }
        bool found = false;
        for (size_t map = 0; map < group_count; map++) {
            if (group_map[map].old_id == entry->peer_group_id) {
                entry->peer_group_id = group_map[map].new_id;
                found = true;
                break;
            }
        }
        if (!found && group_count < MAX_MOUNTS) {
            uint64_t old_id = entry->peer_group_id;
            uint64_t new_id = vfs_mount_next_peer_group_id();
            group_map[group_count].old_id = old_id;
            group_map[group_count].new_id = new_id;
            group_count++;
            entry->peer_group_id = new_id;
        }
    }
    for (size_t i = 0; i < MAX_MOUNTS; i++) {
        struct vfs_mount_entry *entry = &new_ns->entries[i];

        if (!entry->active || entry->propagation != MS_SLAVE || entry->master_group_id == 0) {
            continue;
        }
        for (size_t map = 0; map < group_count; map++) {
            if (group_map[map].old_id == entry->master_group_id) {
                entry->master_group_id = group_map[map].new_id;
                break;
            }
        }
    }
    fs_mutex_unlock(&old->lock);

    return new_ns;
}

static struct vfs_mount_namespace *vfs_task_mount_namespace(void) {
    struct task *task = task_current();

    if (!task || !task->fs) {
        return NULL;
    }

    return task->fs->mnt_ns;
}

static bool vfs_current_has_mount_admin(const struct vfs_mount_namespace *mnt_ns) {
    if (!mnt_ns) {
        return false;
    }
    return cred_has_cap_in_user_namespace(get_current_cred(),
                                          mnt_ns->owner_user_ns_id,
                                          CAP_SYS_ADMIN);
}

static unsigned long vfs_mount_propagation_flags(void) {
    return MS_SHARED | MS_PRIVATE | MS_SLAVE | MS_UNBINDABLE;
}

static unsigned long vfs_mount_attribute_flags(void) {
    return MS_RDONLY | MS_NOSUID | MS_NODEV | MS_NOEXEC;
}

static unsigned long vfs_mount_attribute_flags_internal(void) {
    return MNT_READONLY | MNT_NOSUID | MNT_NODEV | MNT_NOEXEC;
}

static unsigned long vfs_mount_internal_flags_from_uapi(unsigned long flags) {
    unsigned long internal_flags = 0;

    if ((flags & MS_RDONLY) != 0) {
        internal_flags |= MNT_READONLY;
    }
    if ((flags & MS_NOSUID) != 0) {
        internal_flags |= MNT_NOSUID;
    }
    if ((flags & MS_NODEV) != 0) {
        internal_flags |= MNT_NODEV;
    }
    if ((flags & MS_NOEXEC) != 0) {
        internal_flags |= MNT_NOEXEC;
    }

    return internal_flags;
}

static void vfs_mount_apply_remount_attributes(struct vfs_mount_entry *entry,
                                               unsigned long flags) {
    entry->flags = (entry->flags & ~vfs_mount_attribute_flags_internal()) |
                   vfs_mount_internal_flags_from_uapi(flags);
}

static unsigned long vfs_mount_selected_propagation(unsigned long flags) {
    unsigned long propagation = flags & vfs_mount_propagation_flags();
    if ((propagation & (propagation - 1UL)) != 0) {
        return (unsigned long)-1;
    }
    return propagation;
}

static uint64_t vfs_mount_next_id(void) {
    return (uint64_t)atomic_inc_return(&vfs_next_mount_id);
}

static int vfs_mount_copy_entry(struct vfs_mount_entry *entry, const char *source, const char *target,
                                const char *fstype, unsigned long flags, unsigned long propagation) {
    int ret;

    memset(entry, 0, sizeof(*entry));
    entry->active = true;
    ret = vfs_copy_string(source, entry->source, sizeof(entry->source));
    if (ret != 0) {
        memset(entry, 0, sizeof(*entry));
        return ret;
    }
    ret = vfs_copy_string(target, entry->target, sizeof(entry->target));
    if (ret != 0) {
        memset(entry, 0, sizeof(*entry));
        return ret;
    }
    if (fstype && fstype[0] != '\0') {
        ret = vfs_copy_string(fstype, entry->fstype, sizeof(entry->fstype));
        if (ret != 0) {
            memset(entry, 0, sizeof(*entry));
            return ret;
        }
    } else {
        memcpy(entry->fstype, "none", sizeof("none"));
    }
    entry->flags = vfs_mount_internal_flags_from_uapi(flags);
    entry->propagation = propagation;
    entry->peer_group_id = 0;
    entry->master_group_id = 0;
    entry->expiry_candidate = false;
    entry->mount_id = vfs_mount_next_id();
    return 0;
}

static uint64_t vfs_mount_next_peer_group_id(void) {
    return (uint64_t)atomic_inc_return(&vfs_next_mount_peer_group_id);
}

static uint64_t vfs_mount_existing_peer_group_locked(struct vfs_mount_namespace *mnt_ns,
                                                     const struct vfs_mount_entry *entry) {
    if (!mnt_ns || !entry) {
        return 0;
    }
    for (size_t i = 0; i < MAX_MOUNTS; i++) {
        const struct vfs_mount_entry *peer = &mnt_ns->entries[i];
        if (peer == entry || !peer->active || peer->propagation != MS_SHARED ||
            peer->peer_group_id == 0 || strcmp(peer->source, entry->source) != 0) {
            continue;
        }
        return peer->peer_group_id;
    }
    return 0;
}

static void vfs_mount_assign_propagation_ids_locked(struct vfs_mount_namespace *mnt_ns,
                                                    struct vfs_mount_entry *entry) {
    if (!mnt_ns || !entry) {
        return;
    }
    if (entry->propagation == MS_SHARED) {
        if (entry->peer_group_id == 0) {
            entry->peer_group_id = vfs_mount_existing_peer_group_locked(mnt_ns, entry);
            if (entry->peer_group_id == 0) {
                entry->peer_group_id = vfs_mount_next_peer_group_id();
            }
        }
        entry->master_group_id = 0;
        return;
    }
    if (entry->propagation == MS_SLAVE) {
        if (entry->master_group_id == 0) {
            entry->master_group_id = vfs_mount_existing_peer_group_locked(mnt_ns, entry);
            if (entry->master_group_id == 0) {
                entry->master_group_id = mnt_ns->ns_id;
            }
        }
        entry->peer_group_id = 0;
        return;
    }
    entry->peer_group_id = 0;
    entry->master_group_id = 0;
}

static void vfs_mount_set_propagation_locked(struct vfs_mount_namespace *mnt_ns,
                                             struct vfs_mount_entry *entry,
                                             unsigned long propagation) {
    unsigned long old_propagation;
    uint64_t old_peer_group_id;

    if (!mnt_ns || !entry || propagation == 0) {
        return;
    }
    old_propagation = entry->propagation;
    old_peer_group_id = entry->peer_group_id;
    entry->propagation = propagation;
    entry->peer_group_id = 0;
    entry->master_group_id = 0;
    if (propagation == MS_SLAVE && old_propagation == MS_SHARED && old_peer_group_id != 0) {
        entry->master_group_id = old_peer_group_id;
    }
    vfs_mount_assign_propagation_ids_locked(mnt_ns, entry);
}

static void vfs_mount_set_tree_propagation_locked(struct vfs_mount_namespace *mnt_ns,
                                                  const char *target,
                                                  unsigned long propagation) {
    if (!mnt_ns || !target || propagation == 0) {
        return;
    }
    for (size_t i = 0; i < MAX_MOUNTS; i++) {
        if (mnt_ns->entries[i].active &&
            vfs_mount_target_matches_tree(mnt_ns->entries[i].target, target)) {
            vfs_mount_set_propagation_locked(mnt_ns, &mnt_ns->entries[i], propagation);
        }
    }
}

static int vfs_mount_find_free_slot_locked(struct vfs_mount_namespace *mnt_ns) {
    for (size_t i = 0; i < MAX_MOUNTS; i++) {
        if (!mnt_ns->entries[i].active) {
            return (int)i;
        }
    }
    return -1;
}

static int vfs_mount_propagate_shared_child_locked(struct vfs_mount_namespace *mnt_ns, int mounted_slot) {
    struct vfs_mount_entry *mounted;
    struct vfs_mount_entry *parent = NULL;
    size_t parent_len = 0;
    const char *suffix;

    if (!mnt_ns || mounted_slot < 0 || mounted_slot >= MAX_MOUNTS ||
        !mnt_ns->entries[mounted_slot].active) {
        return -EINVAL;
    }

    mounted = &mnt_ns->entries[mounted_slot];
    for (size_t i = 0; i < MAX_MOUNTS; i++) {
        struct vfs_mount_entry *entry = &mnt_ns->entries[i];
        size_t entry_len;

        if ((int)i == mounted_slot || !entry->active || entry->propagation != MS_SHARED ||
            !vfs_path_matches_prefix(mounted->target, entry->target) ||
            strcmp(mounted->target, entry->target) == 0) {
            continue;
        }
        entry_len = strlen(entry->target);
        if (!parent || entry_len > parent_len) {
            parent = entry;
            parent_len = entry_len;
        }
    }

    if (!parent) {
        return 0;
    }

    suffix = mounted->target + parent_len;
    for (size_t i = 0; i < MAX_MOUNTS; i++) {
        struct vfs_mount_entry *peer = &mnt_ns->entries[i];
        char peer_target[MAX_PATH];
        int slot;
        int ret;

        bool shared_peer = peer->propagation == MS_SHARED &&
                           parent->peer_group_id != 0 &&
                           peer->peer_group_id == parent->peer_group_id;
        bool slave_peer = peer->propagation == MS_SLAVE &&
                          parent->peer_group_id != 0 &&
                          peer->master_group_id == parent->peer_group_id;

        if (!peer->active || peer == parent || (!shared_peer && !slave_peer)) {
            continue;
        }

        ret = snprintf(peer_target, sizeof(peer_target), "%s%s", peer->target, suffix);
        if (ret < 0 || (size_t)ret >= sizeof(peer_target)) {
            return -ENAMETOOLONG;
        }

        bool exists = false;
        for (size_t j = 0; j < MAX_MOUNTS; j++) {
            if (mnt_ns->entries[j].active && strcmp(mnt_ns->entries[j].target, peer_target) == 0) {
                exists = true;
                break;
            }
        }
        if (exists) {
            continue;
        }

        slot = vfs_mount_find_free_slot_locked(mnt_ns);
        if (slot < 0) {
            return -ENOSPC;
        }
        ret = vfs_mount_copy_entry(&mnt_ns->entries[slot], mounted->source, peer_target,
                                   mounted->fstype, mounted->flags, mounted->propagation);
        if (ret != 0) {
            return ret;
        }
        mnt_ns->entries[slot].peer_group_id = mounted->peer_group_id;
        mnt_ns->entries[slot].master_group_id = mounted->master_group_id;
        vfs_mount_assign_propagation_ids_locked(mnt_ns, &mnt_ns->entries[slot]);
    }
    return 0;
}

static int vfs_mount_propagate_attached_subtree_locked(struct vfs_mount_namespace *mnt_ns,
                                                       const int *mounted_slots,
                                                       size_t mounted_count) {
    struct vfs_mount_entry *root;
    struct vfs_mount_entry *parent = NULL;
    size_t parent_len = 0;

    if (!mnt_ns || !mounted_slots || mounted_count == 0 ||
        mounted_slots[0] < 0 || mounted_slots[0] >= MAX_MOUNTS ||
        !mnt_ns->entries[mounted_slots[0]].active) {
        return -EINVAL;
    }

    root = &mnt_ns->entries[mounted_slots[0]];
    for (size_t i = 0; i < MAX_MOUNTS; i++) {
        struct vfs_mount_entry *entry = &mnt_ns->entries[i];
        size_t entry_len;

        if (!entry->active || entry->propagation != MS_SHARED ||
            !vfs_path_matches_prefix(root->target, entry->target) ||
            strcmp(root->target, entry->target) == 0) {
            continue;
        }
        entry_len = strlen(entry->target);
        if (!parent || entry_len > parent_len) {
            parent = entry;
            parent_len = entry_len;
        }
    }
    if (!parent) {
        return 0;
    }

    for (size_t peer_idx = 0; peer_idx < MAX_MOUNTS; peer_idx++) {
        struct vfs_mount_entry *peer = &mnt_ns->entries[peer_idx];
        bool shared_peer;
        bool slave_peer;

        if (!peer->active || peer == parent) {
            continue;
        }
        shared_peer = peer->propagation == MS_SHARED &&
                      parent->peer_group_id != 0 &&
                      peer->peer_group_id == parent->peer_group_id;
        slave_peer = peer->propagation == MS_SLAVE &&
                     parent->peer_group_id != 0 &&
                     peer->master_group_id == parent->peer_group_id;
        if (!shared_peer && !slave_peer) {
            continue;
        }

        for (size_t mounted_idx = 0; mounted_idx < mounted_count; mounted_idx++) {
            struct vfs_mount_entry *mounted;
            char peer_target[MAX_PATH];
            const char *suffix;
            int slot;
            int ret;
            bool exists = false;

            if (mounted_slots[mounted_idx] < 0 || mounted_slots[mounted_idx] >= MAX_MOUNTS ||
                !mnt_ns->entries[mounted_slots[mounted_idx]].active) {
                return -EINVAL;
            }
            mounted = &mnt_ns->entries[mounted_slots[mounted_idx]];
            suffix = mounted->target + parent_len;
            ret = snprintf(peer_target, sizeof(peer_target), "%s%s", peer->target, suffix);
            if (ret < 0 || (size_t)ret >= sizeof(peer_target)) {
                return -ENAMETOOLONG;
            }

            for (size_t scan = 0; scan < MAX_MOUNTS; scan++) {
                if (mnt_ns->entries[scan].active &&
                    strcmp(mnt_ns->entries[scan].target, peer_target) == 0) {
                    exists = true;
                    break;
                }
            }
            if (exists) {
                continue;
            }

            slot = vfs_mount_find_free_slot_locked(mnt_ns);
            if (slot < 0) {
                return -ENOSPC;
            }
            ret = vfs_mount_copy_entry(&mnt_ns->entries[slot], mounted->source, peer_target,
                                       mounted->fstype, mounted->flags, mounted->propagation);
            if (ret != 0) {
                return ret;
            }
            mnt_ns->entries[slot].peer_group_id = mounted->peer_group_id;
            mnt_ns->entries[slot].master_group_id = mounted->master_group_id;
            vfs_mount_assign_propagation_ids_locked(mnt_ns, &mnt_ns->entries[slot]);
        }
    }

    return 0;
}

static bool vfs_mount_target_matches_tree(const char *target, const char *root) {
    return target && root && (strcmp(target, root) == 0 || vfs_path_matches_prefix(target, root));
}

static void vfs_umount_remove_tree_locked(struct vfs_mount_namespace *mnt_ns, const char *target) {
    if (!mnt_ns || !target) {
        return;
    }
    for (size_t i = 0; i < MAX_MOUNTS; i++) {
        if (mnt_ns->entries[i].active && vfs_mount_target_matches_tree(mnt_ns->entries[i].target, target)) {
            memset(&mnt_ns->entries[i], 0, sizeof(mnt_ns->entries[i]));
        }
    }
}

static void vfs_umount_propagate_shared_child_locked(struct vfs_mount_namespace *mnt_ns,
                                                     const char *target) {
    struct vfs_mount_entry *parent = NULL;
    struct vfs_mount_entry *target_entry = NULL;
    size_t parent_len = 0;
    const char *suffix;

    if (!mnt_ns || !target) {
        return;
    }

    for (size_t i = 0; i < MAX_MOUNTS; i++) {
        struct vfs_mount_entry *entry = &mnt_ns->entries[i];
        size_t entry_len;

        if (entry->active && strcmp(entry->target, target) == 0) {
            target_entry = entry;
        }
        if (!entry->active || entry->propagation != MS_SHARED ||
            !vfs_path_matches_prefix(target, entry->target) ||
            strcmp(target, entry->target) == 0) {
            continue;
        }
        entry_len = strlen(entry->target);
        if (!parent || entry_len > parent_len) {
            parent = entry;
            parent_len = entry_len;
        }
    }

    if (!parent) {
        return;
    }
    if (target_entry &&
        (target_entry->propagation == MS_PRIVATE ||
         target_entry->propagation == MS_SLAVE ||
         target_entry->propagation == MS_UNBINDABLE)) {
        return;
    }

    suffix = target + parent_len;
    for (size_t i = 0; i < MAX_MOUNTS; i++) {
        struct vfs_mount_entry *peer = &mnt_ns->entries[i];
        char peer_target[MAX_PATH];
        int ret;

        bool shared_peer = peer->propagation == MS_SHARED &&
                           parent->peer_group_id != 0 &&
                           peer->peer_group_id == parent->peer_group_id;
        bool slave_peer = peer->propagation == MS_SLAVE &&
                          parent->peer_group_id != 0 &&
                          peer->master_group_id == parent->peer_group_id;

        if (!peer->active || peer == parent || (!shared_peer && !slave_peer)) {
            continue;
        }

        ret = snprintf(peer_target, sizeof(peer_target), "%s%s", peer->target, suffix);
        if (ret < 0 || (size_t)ret >= sizeof(peer_target)) {
            continue;
        }
        vfs_umount_remove_tree_locked(mnt_ns, peer_target);
    }
}

static void vfs_umount_propagate_tree_locked(struct vfs_mount_namespace *mnt_ns, const char *target) {
    struct vfs_mount_entry snapshots[MAX_MOUNTS];
    bool propagated[MAX_MOUNTS] = {false};

    if (!mnt_ns || !target) {
        return;
    }

    memcpy(snapshots, mnt_ns->entries, sizeof(snapshots));
    for (;;) {
        size_t best = MAX_MOUNTS;
        size_t best_len = 0;

        for (size_t i = 0; i < MAX_MOUNTS; i++) {
            size_t target_len;

            if (propagated[i] || !snapshots[i].active ||
                !vfs_mount_target_matches_tree(snapshots[i].target, target)) {
                continue;
            }
            target_len = strlen(snapshots[i].target);
            if (best == MAX_MOUNTS || target_len > best_len) {
                best = i;
                best_len = target_len;
            }
        }
        if (best == MAX_MOUNTS) {
            break;
        }
        vfs_umount_propagate_shared_child_locked(mnt_ns, snapshots[best].target);
        propagated[best] = true;
    }
}

static bool vfs_task_fs_path_pins_mount(const struct task *task, const char *target) {
    bool pins = false;

    if (!task || !task->fs || !target) {
        return false;
    }
    fs_mutex_lock(&task->fs->lock);
    pins = vfs_mount_target_matches_tree(task->fs->pwd_path, target) ||
           vfs_mount_target_matches_tree(task->fs->root_path, target);
    fs_mutex_unlock(&task->fs->lock);
    return pins;
}

static bool vfs_any_task_fs_pins_mount_tree(uint64_t mount_ns_id, const char *target) {
    bool pins = false;

    if (mount_ns_id == 0 || !target) {
        return false;
    }
    kernel_mutex_lock(&task_table_lock);
    for (int i = 0; i < TASK_MAX_TASKS && !pins; i++) {
        struct task *task = task_table[i];

        while (task) {
            if (task->fs && fs_mount_namespace_id(task->fs) == mount_ns_id &&
                vfs_task_fs_path_pins_mount(task, target)) {
                pins = true;
                break;
            }
            task = task->hash_next;
        }
    }
    kernel_mutex_unlock(&task_table_lock);
    return pins;
}

static unsigned int vfs_proc_task_thread_count(const struct task *task) {
    unsigned int count = 0;

    if (!task) {
        return 0;
    }

    kernel_mutex_lock(&task_table_lock);
    for (int i = 0; i < TASK_MAX_TASKS; i++) {
        struct task *cursor = task_table[i];

        while (cursor) {
            if (cursor->tgid == task->tgid) {
                count++;
            }
            cursor = cursor->hash_next;
        }
    }
    kernel_mutex_unlock(&task_table_lock);

    return count ? count : 1;
}

static int vfs_detached_mount_record(uint64_t mount_ns_id, const char *target) {
    int free_slot = -1;

    if (mount_ns_id == 0 || !target) {
        return -EINVAL;
    }
    fs_mutex_lock(&vfs_detached_mount_lock);
    for (size_t i = 0; i < VFS_DETACHED_MOUNT_MAX; i++) {
        if (vfs_detached_mount_refs[i].active &&
            vfs_detached_mount_refs[i].mount_ns_id == mount_ns_id &&
            strcmp(vfs_detached_mount_refs[i].target, target) == 0) {
            fs_mutex_unlock(&vfs_detached_mount_lock);
            return 0;
        }
        if (!vfs_detached_mount_refs[i].active && free_slot < 0) {
            free_slot = (int)i;
        }
    }
    if (free_slot < 0) {
        fs_mutex_unlock(&vfs_detached_mount_lock);
        return -ENOSPC;
    }
    vfs_detached_mount_refs[free_slot].active = true;
    vfs_detached_mount_refs[free_slot].mount_ns_id = mount_ns_id;
    vfs_copy_string(target, vfs_detached_mount_refs[free_slot].target,
                    sizeof(vfs_detached_mount_refs[free_slot].target));
    fs_mutex_unlock(&vfs_detached_mount_lock);
    return 0;
}

unsigned int vfs_detached_mount_ref_count(void) {
    unsigned int count = 0;

    fs_mutex_lock(&vfs_detached_mount_lock);
    for (size_t i = 0; i < VFS_DETACHED_MOUNT_MAX; i++) {
        if (vfs_detached_mount_refs[i].active) {
            count++;
        }
    }
    fs_mutex_unlock(&vfs_detached_mount_lock);
    return count;
}

static void vfs_detached_mount_drop_namespace_refs(uint64_t mount_ns_id) {
    fs_mutex_lock(&vfs_detached_mount_lock);
    for (size_t i = 0; i < VFS_DETACHED_MOUNT_MAX; i++) {
        if (vfs_detached_mount_refs[i].active &&
            vfs_detached_mount_refs[i].mount_ns_id == mount_ns_id) {
            memset(&vfs_detached_mount_refs[i], 0, sizeof(vfs_detached_mount_refs[i]));
        }
    }
    fs_mutex_unlock(&vfs_detached_mount_lock);
}

int vfs_reap_detached_mount_refs(void) {
    int reaped = 0;

    for (size_t i = 0; i < VFS_DETACHED_MOUNT_MAX; i++) {
        char target[MAX_PATH];
        uint64_t mount_ns_id;
        bool active;

        fs_mutex_lock(&vfs_detached_mount_lock);
        active = vfs_detached_mount_refs[i].active;
        if (!vfs_detached_mount_refs[i].active) {
            fs_mutex_unlock(&vfs_detached_mount_lock);
            continue;
        }
        mount_ns_id = vfs_detached_mount_refs[i].mount_ns_id;
        vfs_copy_string(vfs_detached_mount_refs[i].target, target, sizeof(target));
        fs_mutex_unlock(&vfs_detached_mount_lock);

        if (!fdtable_has_open_path_under_mount_namespace_impl(mount_ns_id, target) &&
            !vfs_any_task_fs_pins_mount_tree(mount_ns_id, target)) {
            fs_mutex_lock(&vfs_detached_mount_lock);
            if (active && vfs_detached_mount_refs[i].active &&
                vfs_detached_mount_refs[i].mount_ns_id == mount_ns_id &&
                strcmp(vfs_detached_mount_refs[i].target, target) == 0) {
                memset(&vfs_detached_mount_refs[i], 0, sizeof(vfs_detached_mount_refs[i]));
                reaped++;
            }
            fs_mutex_unlock(&vfs_detached_mount_lock);
        }
    }
    return reaped;
}

static int vfs_mount_clone_recursive_children_locked(struct vfs_mount_namespace *mnt_ns,
                                                     int mounted_slot) {
    struct vfs_mount_entry snapshots[MAX_MOUNTS];
    struct vfs_mount_entry *mounted;

    if (!mnt_ns || mounted_slot < 0 || mounted_slot >= MAX_MOUNTS ||
        !mnt_ns->entries[mounted_slot].active) {
        return -EINVAL;
    }

    mounted = &mnt_ns->entries[mounted_slot];
    memcpy(snapshots, mnt_ns->entries, sizeof(snapshots));

    for (size_t i = 0; i < MAX_MOUNTS; i++) {
        const struct vfs_mount_entry *child = &snapshots[i];
        const char *suffix;
        char child_target[MAX_PATH];
        int slot;
        int ret;
        bool exists = false;

        if (!child->active || strcmp(child->target, mounted->source) == 0 ||
            !vfs_path_matches_prefix(child->target, mounted->source)) {
            continue;
        }

        suffix = child->target + strlen(mounted->source);
        ret = snprintf(child_target, sizeof(child_target), "%s%s", mounted->target, suffix);
        if (ret < 0 || (size_t)ret >= sizeof(child_target)) {
            return -ENAMETOOLONG;
        }

        for (size_t j = 0; j < MAX_MOUNTS; j++) {
            if (mnt_ns->entries[j].active && strcmp(mnt_ns->entries[j].target, child_target) == 0) {
                exists = true;
                break;
            }
        }
        if (exists) {
            continue;
        }

        slot = vfs_mount_find_free_slot_locked(mnt_ns);
        if (slot < 0) {
            return -ENOSPC;
        }
        ret = vfs_mount_copy_entry(&mnt_ns->entries[slot], child->source, child_target,
                                   child->fstype, child->flags, child->propagation);
        if (ret != 0) {
            return ret;
        }
        mnt_ns->entries[slot].peer_group_id = child->peer_group_id;
        mnt_ns->entries[slot].master_group_id = child->master_group_id;
        vfs_mount_assign_propagation_ids_locked(mnt_ns, &mnt_ns->entries[slot]);
    }

    return 0;
}

static int vfs_mount_move_tree_locked(struct vfs_mount_namespace *mnt_ns,
                                      const char *old_target,
                                      const char *new_target) {
    bool found = false;
    size_t old_len;

    if (!mnt_ns || !old_target || !new_target || strcmp(old_target, new_target) == 0) {
        return -EINVAL;
    }

    old_len = strlen(old_target);
    for (size_t i = 0; i < MAX_MOUNTS; i++) {
        if (mnt_ns->entries[i].active && strcmp(mnt_ns->entries[i].target, old_target) == 0) {
            found = true;
        }
        if (mnt_ns->entries[i].active && strcmp(mnt_ns->entries[i].target, new_target) == 0) {
            return -EBUSY;
        }
    }
    if (!found) {
        return -EINVAL;
    }

    for (size_t i = 0; i < MAX_MOUNTS; i++) {
        struct vfs_mount_entry *entry = &mnt_ns->entries[i];
        const char *suffix;
        char moved_target[MAX_PATH];
        int ret;

        if (!entry->active || !vfs_mount_target_matches_tree(entry->target, old_target)) {
            continue;
        }
        suffix = entry->target + old_len;
        ret = snprintf(moved_target, sizeof(moved_target), "%s%s", new_target, suffix);
        if (ret < 0 || (size_t)ret >= sizeof(moved_target)) {
            return -ENAMETOOLONG;
        }
        ret = vfs_copy_string(moved_target, entry->target, sizeof(entry->target));
        if (ret != 0) {
            return ret;
        }
    }

    return 0;
}

struct vfs_mount_move_plan {
    char old_target[MAX_PATH];
    char new_target[MAX_PATH];
};

static int vfs_mount_plan_shared_move_locked(struct vfs_mount_namespace *mnt_ns,
                                             const char *old_target,
                                             const char *new_target,
                                             struct vfs_mount_move_plan *plans,
                                             size_t max_plans,
                                             size_t *plan_count) {
    struct vfs_mount_entry *parent = NULL;
    struct vfs_mount_entry *source = NULL;
    size_t parent_len = 0;
    const char *old_suffix;
    const char *new_suffix;

    if (!mnt_ns || !old_target || !new_target || !plans || !plan_count || *plan_count >= max_plans) {
        return -EINVAL;
    }

    for (size_t i = 0; i < MAX_MOUNTS; i++) {
        struct vfs_mount_entry *entry = &mnt_ns->entries[i];
        size_t entry_len;

        if (entry->active && strcmp(entry->target, old_target) == 0) {
            source = entry;
        }
        if (!entry->active || entry->propagation != MS_SHARED ||
            !vfs_path_matches_prefix(old_target, entry->target) ||
            strcmp(old_target, entry->target) == 0 ||
            !vfs_path_matches_prefix(new_target, entry->target)) {
            continue;
        }
        entry_len = strlen(entry->target);
        if (!parent || entry_len > parent_len) {
            parent = entry;
            parent_len = entry_len;
        }
    }

    if (source &&
        (source->propagation == MS_PRIVATE ||
         source->propagation == MS_SLAVE ||
         source->propagation == MS_UNBINDABLE)) {
        return 0;
    }

    if (!parent) {
        return 0;
    }

    old_suffix = old_target + parent_len;
    new_suffix = new_target + parent_len;
    for (size_t i = 0; i < MAX_MOUNTS; i++) {
        struct vfs_mount_entry *peer = &mnt_ns->entries[i];
        char peer_old[MAX_PATH];
        char peer_new[MAX_PATH];
        int ret;
        bool shared_peer = false;
        bool slave_peer = false;
        bool has_peer_old = false;

        if (!peer->active || peer == parent) {
            continue;
        }
        shared_peer = peer->propagation == MS_SHARED &&
                      parent->peer_group_id != 0 &&
                      peer->peer_group_id == parent->peer_group_id;
        slave_peer = peer->propagation == MS_SLAVE &&
                     parent->peer_group_id != 0 &&
                     peer->master_group_id == parent->peer_group_id;
        if (!shared_peer && !slave_peer) {
            continue;
        }

        ret = snprintf(peer_old, sizeof(peer_old), "%s%s", peer->target, old_suffix);
        if (ret < 0 || (size_t)ret >= sizeof(peer_old)) {
            return -ENAMETOOLONG;
        }
        ret = snprintf(peer_new, sizeof(peer_new), "%s%s", peer->target, new_suffix);
        if (ret < 0 || (size_t)ret >= sizeof(peer_new)) {
            return -ENAMETOOLONG;
        }

        for (size_t scan = 0; scan < MAX_MOUNTS; scan++) {
            if (mnt_ns->entries[scan].active && strcmp(mnt_ns->entries[scan].target, peer_old) == 0) {
                has_peer_old = true;
                break;
            }
        }
        if (!has_peer_old) {
            continue;
        }
        if (*plan_count >= max_plans) {
            return -ENOSPC;
        }
        ret = vfs_copy_string(peer_old, plans[*plan_count].old_target, sizeof(plans[*plan_count].old_target));
        if (ret == 0) {
            ret = vfs_copy_string(peer_new, plans[*plan_count].new_target, sizeof(plans[*plan_count].new_target));
        }
        if (ret != 0) {
            return ret;
        }
        (*plan_count)++;
    }

    return 0;
}

static int vfs_mount_move_tree_with_propagation_locked(struct vfs_mount_namespace *mnt_ns,
                                                       const char *old_target,
                                                       const char *new_target) {
    struct vfs_mount_move_plan plans[MAX_MOUNTS];
    size_t plan_count = 1;
    int ret;

    if (!mnt_ns || !old_target || !new_target || strcmp(old_target, new_target) == 0) {
        return -EINVAL;
    }
    ret = vfs_copy_string(old_target, plans[0].old_target, sizeof(plans[0].old_target));
    if (ret == 0) {
        ret = vfs_copy_string(new_target, plans[0].new_target, sizeof(plans[0].new_target));
    }
    if (ret != 0) {
        return ret;
    }
    ret = vfs_mount_plan_shared_move_locked(mnt_ns, old_target, new_target,
                                            plans, MAX_MOUNTS, &plan_count);
    if (ret != 0) {
        return ret;
    }

    for (size_t i = 0; i < plan_count; i++) {
        for (size_t j = 0; j < MAX_MOUNTS; j++) {
            if (mnt_ns->entries[j].active &&
                strcmp(mnt_ns->entries[j].target, plans[i].new_target) == 0) {
                return -EBUSY;
            }
        }
    }

    for (size_t i = 0; i < plan_count; i++) {
        ret = vfs_mount_move_tree_locked(mnt_ns, plans[i].old_target, plans[i].new_target);
        if (ret != 0) {
            return ret;
        }
    }
    return 0;
}

struct vfs_route_entry {
    enum vfs_route_identity route_id;
    const char *linux_prefix;
    enum vfs_backing_class backing_class;
    bool synthetic;
    bool strip_linux_prefix;
    const char *reverse_linux_prefix;
};

static const struct vfs_route_entry vfs_route_table[] = {
    {VFS_ROUTE_VAR_CACHE, "/var/cache", VFS_BACKING_CACHE, false, true, "/var/cache"},
    {VFS_ROUTE_TMP, "/tmp", VFS_BACKING_TEMP, false, true, "/tmp"},
    {VFS_ROUTE_VAR_TMP, "/var/tmp", VFS_BACKING_TEMP, false, true, NULL},
    {VFS_ROUTE_RUN, "/run", VFS_BACKING_TEMP, false, true, NULL},
    {VFS_ROUTE_PROC, "/proc", VFS_BACKING_SYNTHETIC, true, false, NULL},
    {VFS_ROUTE_SYS, "/sys", VFS_BACKING_SYNTHETIC, true, false, NULL},
    {VFS_ROUTE_DEV, "/dev", VFS_BACKING_SYNTHETIC, true, false, NULL},
    {VFS_ROUTE_ETC, "/etc", VFS_BACKING_PERSISTENT, false, false, "/etc"},
    {VFS_ROUTE_USR, "/usr", VFS_BACKING_PERSISTENT, false, false, "/usr"},
    {VFS_ROUTE_VAR_LIB, "/var/lib", VFS_BACKING_PERSISTENT, false, false, "/var/lib"},
    {VFS_ROUTE_HOME, "/home", VFS_BACKING_PERSISTENT, false, false, "/home"},
    {VFS_ROUTE_ROOT_HOME, "/root", VFS_BACKING_PERSISTENT, false, false, "/root"},
    {VFS_ROUTE_PERSISTENT_ROOT, "/", VFS_BACKING_PERSISTENT, false, false, "/"},
};

static const size_t vfs_route_table_count = sizeof(vfs_route_table) / sizeof(vfs_route_table[0]);

static int vfs_copy_string(const char *src, char *dst, size_t dst_len) {
    size_t len;

    if (!src || !dst || dst_len == 0) {
        return -EINVAL;
    }

    len = strlen(src);
    if (len >= dst_len) {
        return -ENAMETOOLONG;
    }

    memcpy(dst, src, len + 1);
    return 0;
}

static int vfs_metadata_find_locked(const char *resolved_vpath) {
    if (!resolved_vpath) {
        return -1;
    }

    for (size_t i = 0; i < VFS_METADATA_MAX; i++) {
        if (vfs_metadata_table[i].active && strcmp(vfs_metadata_table[i].path, resolved_vpath) == 0) {
            return (int)i;
        }
    }

    return -1;
}

static uint32_t vfs_default_mode_for_virtual_path(const char *resolved_vpath, uint32_t current_mode) {
    if (!resolved_vpath) {
        return current_mode;
    }

    if (strcmp(resolved_vpath, "/tmp") == 0 || strcmp(resolved_vpath, "/var/tmp") == 0 ||
        strcmp(resolved_vpath, "/run") == 0) {
        return (current_mode & ~07777U) | 0777U;
    }

    return current_mode;
}

void vfs_apply_stat_metadata(const char *resolved_vpath, struct stat *statbuf) {
    int idx;

    if (!statbuf) {
        return;
    }

    statbuf->st_uid = 0;
    statbuf->st_gid = 0;
    statbuf->st_mode = vfs_default_mode_for_virtual_path(resolved_vpath, statbuf->st_mode);

    fs_mutex_lock(&vfs_metadata_lock);
    idx = vfs_metadata_find_locked(resolved_vpath);
    if (idx >= 0 && vfs_metadata_table[idx].has_attrs) {
        uint32_t file_type = statbuf->st_mode & ~07777U;
        statbuf->st_uid = vfs_metadata_table[idx].uid;
        statbuf->st_gid = vfs_metadata_table[idx].gid;
        statbuf->st_mode = file_type | (vfs_metadata_table[idx].mode & 07777U);
    }
    if (idx >= 0 && vfs_metadata_table[idx].has_times) {
        statbuf->st_atime = vfs_metadata_table[idx].atime_sec;
        statbuf->st_atime_nsec = vfs_metadata_table[idx].atime_nsec;
        statbuf->st_mtime = vfs_metadata_table[idx].mtime_sec;
        statbuf->st_mtime_nsec = vfs_metadata_table[idx].mtime_nsec;
    }
    fs_mutex_unlock(&vfs_metadata_lock);
}

void vfs_record_created_path(const char *resolved_vpath, uint32_t mode) {
    struct cred *cred = get_current_cred();
    int idx;
    int free_slot = -1;

    if (!resolved_vpath || !cred) {
        return;
    }

    fs_mutex_lock(&vfs_metadata_lock);
    idx = vfs_metadata_find_locked(resolved_vpath);
    if (idx < 0) {
        for (size_t i = 0; i < VFS_METADATA_MAX; i++) {
            if (!vfs_metadata_table[i].active) {
                free_slot = (int)i;
                break;
            }
        }
        idx = free_slot;
    }

    if (idx >= 0) {
        vfs_metadata_table[idx].active = true;
        vfs_copy_string(resolved_vpath, vfs_metadata_table[idx].path, sizeof(vfs_metadata_table[idx].path));
        if (vfs_metadata_table[idx].file_identity == 0) {
            vfs_metadata_table[idx].file_identity = (uint64_t)atomic_inc_return(&vfs_next_file_identity);
        }
        vfs_metadata_table[idx].has_attrs = true;
        vfs_metadata_table[idx].uid = cred->fsuid;
        vfs_metadata_table[idx].gid = cred->fsgid;
        vfs_metadata_table[idx].mode = mode & 07777U;
    }
    fs_mutex_unlock(&vfs_metadata_lock);
}

static int vfs_record_metadata_for_stat(const char *resolved_vpath, const struct stat *st) {
    int idx;
    int free_slot = -1;

    if (!resolved_vpath || !st) {
        return -EINVAL;
    }

    idx = vfs_metadata_find_locked(resolved_vpath);
    if (idx < 0) {
        for (size_t i = 0; i < VFS_METADATA_MAX; i++) {
            if (!vfs_metadata_table[i].active) {
                free_slot = (int)i;
                break;
            }
        }
        idx = free_slot;
    }
    if (idx < 0) {
        return -ENOSPC;
    }

    vfs_metadata_table[idx].active = true;
    vfs_copy_string(resolved_vpath, vfs_metadata_table[idx].path, sizeof(vfs_metadata_table[idx].path));
    if (vfs_metadata_table[idx].file_identity == 0) {
        vfs_metadata_table[idx].file_identity = (uint64_t)atomic_inc_return(&vfs_next_file_identity);
    }
    vfs_metadata_table[idx].has_attrs = true;
    vfs_metadata_table[idx].uid = st->st_uid;
    vfs_metadata_table[idx].gid = st->st_gid;
    vfs_metadata_table[idx].mode = st->st_mode & 07777U;
    return 0;
}

uint64_t vfs_file_identity_for_path(const char *resolved_vpath) {
    int idx;
    int free_slot = -1;
    uint64_t identity = 0;

    if (!resolved_vpath) {
        return 0;
    }

    fs_mutex_lock(&vfs_metadata_lock);
    idx = vfs_metadata_find_locked(resolved_vpath);
    if (idx < 0) {
        for (size_t i = 0; i < VFS_METADATA_MAX; i++) {
            if (!vfs_metadata_table[i].active) {
                free_slot = (int)i;
                break;
            }
        }
        idx = free_slot;
        if (idx >= 0) {
            vfs_metadata_table[idx].active = true;
            vfs_copy_string(resolved_vpath, vfs_metadata_table[idx].path,
                            sizeof(vfs_metadata_table[idx].path));
            vfs_metadata_table[idx].file_identity =
                (uint64_t)atomic_inc_return(&vfs_next_file_identity);
        }
    }
    if (idx >= 0) {
        if (vfs_metadata_table[idx].file_identity == 0) {
            vfs_metadata_table[idx].file_identity =
                (uint64_t)atomic_inc_return(&vfs_next_file_identity);
        }
        identity = vfs_metadata_table[idx].file_identity;
    }
    fs_mutex_unlock(&vfs_metadata_lock);
    return identity;
}

static int vfs_resolve_existing_metadata_path_follow(const char *path, char *resolved,
                                                     size_t resolved_len,
                                                     int follow_final_symlink) {
    struct stat st;
    int ret;

    if (!path || !resolved || resolved_len == 0) {
        return -EFAULT;
    }
    ret = vfs_resolve_virtual_path_at_follow(AT_FDCWD, path, resolved, resolved_len,
                                             follow_final_symlink);
    if (ret != 0) {
        return ret;
    }
    ret = vfs_fstatat(AT_FDCWD, resolved, &st,
                      follow_final_symlink ? 0 : AT_SYMLINK_NOFOLLOW);
    if (ret != 0) {
        return ret;
    }
    return 0;
}

static int vfs_metadata_ensure_locked(const char *resolved_vpath) {
    int idx;

    idx = vfs_metadata_find_locked(resolved_vpath);
    if (idx >= 0) {
        if (vfs_metadata_table[idx].file_identity == 0) {
            vfs_metadata_table[idx].file_identity =
                (uint64_t)atomic_inc_return(&vfs_next_file_identity);
        }
        return idx;
    }
    for (size_t i = 0; i < VFS_METADATA_MAX; i++) {
        if (!vfs_metadata_table[i].active) {
            vfs_metadata_table[i].active = true;
            vfs_copy_string(resolved_vpath, vfs_metadata_table[i].path,
                            sizeof(vfs_metadata_table[i].path));
            vfs_metadata_table[i].file_identity =
                (uint64_t)atomic_inc_return(&vfs_next_file_identity);
            return (int)i;
        }
    }
    return -1;
}

static int vfs_user_xattr_find_locked(const struct vfs_metadata_entry *entry, const char *name) {
    for (size_t i = 0; i < VFS_USER_XATTR_MAX; i++) {
        if (entry->user_xattrs[i].active &&
            strcmp(entry->user_xattrs[i].name, name) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static void vfs_user_xattr_sync_identity_locked(uint64_t identity,
                                                const struct vfs_user_xattr_entry *source) {
    for (size_t i = 0; i < VFS_METADATA_MAX; i++) {
        struct vfs_metadata_entry *entry = &vfs_metadata_table[i];
        int idx;

        if (!entry->active || entry->file_identity != identity || !source) {
            continue;
        }
        idx = vfs_user_xattr_find_locked(entry, source->name);
        if (idx < 0) {
            for (size_t slot = 0; slot < VFS_USER_XATTR_MAX; slot++) {
                if (!entry->user_xattrs[slot].active) {
                    idx = (int)slot;
                    break;
                }
            }
        }
        if (idx >= 0) {
            entry->user_xattrs[idx] = *source;
        }
    }
}

static void vfs_user_xattr_remove_identity_locked(uint64_t identity, const char *name) {
    for (size_t i = 0; i < VFS_METADATA_MAX; i++) {
        struct vfs_metadata_entry *entry = &vfs_metadata_table[i];
        int idx;

        if (!entry->active || entry->file_identity != identity) {
            continue;
        }
        idx = vfs_user_xattr_find_locked(entry, name);
        if (idx >= 0) {
            memset(&entry->user_xattrs[idx], 0, sizeof(entry->user_xattrs[idx]));
        }
    }
}

static void vfs_metadata_sync_identity_attrs_locked(uint64_t identity,
                                                    uint32_t uid,
                                                    uint32_t gid,
                                                    uint32_t mode) {
    if (identity == 0) {
        return;
    }
    for (size_t i = 0; i < VFS_METADATA_MAX; i++) {
        struct vfs_metadata_entry *entry = &vfs_metadata_table[i];

        if (!entry->active || entry->file_identity != identity) {
            continue;
        }
        entry->has_attrs = true;
        entry->uid = uid;
        entry->gid = gid;
        entry->mode = mode & 07777U;
    }
}

static void vfs_metadata_sync_identity_times_locked(uint64_t identity,
                                                    long atime_sec,
                                                    unsigned long atime_nsec,
                                                    long mtime_sec,
                                                    unsigned long mtime_nsec) {
    if (identity == 0) {
        return;
    }
    for (size_t i = 0; i < VFS_METADATA_MAX; i++) {
        struct vfs_metadata_entry *entry = &vfs_metadata_table[i];

        if (!entry->active || entry->file_identity != identity) {
            continue;
        }
        entry->has_times = true;
        entry->atime_sec = atime_sec;
        entry->atime_nsec = atime_nsec;
        entry->mtime_sec = mtime_sec;
        entry->mtime_nsec = mtime_nsec;
    }
}

int vfs_set_user_xattr_follow(const char *path, const char *name, const void *value, size_t size,
                              int flags, int follow_final_symlink) {
    char resolved[MAX_PATH];
    struct vfs_user_xattr_entry updated;
    int idx;
    int xidx;
    int free_xidx = -1;
    uint64_t identity;
    int ret;

    if (!name || (!value && size > 0)) {
        return -EFAULT;
    }
    if (strncmp(name, "user.", 5) != 0) {
        return -EOPNOTSUPP;
    }
    if (name[5] == '\0' || strlen(name) > VFS_USER_XATTR_NAME_MAX ||
        size > VFS_USER_XATTR_VALUE_MAX) {
        return -ERANGE;
    }
    if ((flags & ~(XATTR_CREATE | XATTR_REPLACE)) != 0 ||
        ((flags & XATTR_CREATE) != 0 && (flags & XATTR_REPLACE) != 0)) {
        return -EINVAL;
    }

    ret = vfs_resolve_existing_metadata_path_follow(path, resolved, sizeof(resolved),
                                                    follow_final_symlink);
    if (ret != 0) {
        return ret;
    }

    fs_mutex_lock(&vfs_metadata_lock);
    idx = vfs_metadata_ensure_locked(resolved);
    if (idx < 0) {
        fs_mutex_unlock(&vfs_metadata_lock);
        return -ENOSPC;
    }
    xidx = vfs_user_xattr_find_locked(&vfs_metadata_table[idx], name);
    if ((flags & XATTR_CREATE) != 0 && xidx >= 0) {
        fs_mutex_unlock(&vfs_metadata_lock);
        return -EEXIST;
    }
    if ((flags & XATTR_REPLACE) != 0 && xidx < 0) {
        fs_mutex_unlock(&vfs_metadata_lock);
        return -ENODATA;
    }
    if (xidx < 0) {
        for (size_t i = 0; i < VFS_USER_XATTR_MAX; i++) {
            if (!vfs_metadata_table[idx].user_xattrs[i].active) {
                free_xidx = (int)i;
                break;
            }
        }
        xidx = free_xidx;
    }
    if (xidx < 0) {
        fs_mutex_unlock(&vfs_metadata_lock);
        return -ENOSPC;
    }
    memset(&updated, 0, sizeof(updated));
    updated.active = true;
    vfs_copy_string(name, updated.name, sizeof(updated.name));
    updated.size = size;
    if (size > 0) {
        memcpy(updated.value, value, size);
    }
    vfs_metadata_table[idx].user_xattrs[xidx] = updated;
    identity = vfs_metadata_table[idx].file_identity;
    vfs_user_xattr_sync_identity_locked(identity, &updated);
    fs_mutex_unlock(&vfs_metadata_lock);
    return 0;
}

int vfs_set_user_xattr(const char *path, const char *name, const void *value, size_t size, int flags) {
    return vfs_set_user_xattr_follow(path, name, value, size, flags, 1);
}

long vfs_get_user_xattr_follow(const char *path, const char *name, void *value, size_t size,
                               int follow_final_symlink) {
    char resolved[MAX_PATH];
    int idx;
    int xidx;
    size_t value_size;
    int ret;

    if (!name) {
        return -EFAULT;
    }
    if (strncmp(name, "user.", 5) != 0) {
        return -ENODATA;
    }
    ret = vfs_resolve_existing_metadata_path_follow(path, resolved, sizeof(resolved),
                                                    follow_final_symlink);
    if (ret != 0) {
        return ret;
    }

    fs_mutex_lock(&vfs_metadata_lock);
    idx = vfs_metadata_find_locked(resolved);
    if (idx < 0) {
        fs_mutex_unlock(&vfs_metadata_lock);
        return -ENODATA;
    }
    xidx = vfs_user_xattr_find_locked(&vfs_metadata_table[idx], name);
    if (xidx < 0) {
        fs_mutex_unlock(&vfs_metadata_lock);
        return -ENODATA;
    }
    value_size = vfs_metadata_table[idx].user_xattrs[xidx].size;
    if (value) {
        if (size < value_size) {
            fs_mutex_unlock(&vfs_metadata_lock);
            return -ERANGE;
        }
        memcpy(value, vfs_metadata_table[idx].user_xattrs[xidx].value, value_size);
    }
    fs_mutex_unlock(&vfs_metadata_lock);
    return (long)value_size;
}

long vfs_get_user_xattr(const char *path, const char *name, void *value, size_t size) {
    return vfs_get_user_xattr_follow(path, name, value, size, 1);
}

long vfs_list_xattr_follow(const char *path, char *list, size_t size, int follow_final_symlink) {
    char resolved[MAX_PATH];
    int idx;
    size_t required = 0;
    size_t pos = 0;
    int ret;

    ret = vfs_resolve_existing_metadata_path_follow(path, resolved, sizeof(resolved),
                                                    follow_final_symlink);
    if (ret != 0) {
        return ret;
    }

    fs_mutex_lock(&vfs_metadata_lock);
    idx = vfs_metadata_find_locked(resolved);
    if (idx < 0) {
        fs_mutex_unlock(&vfs_metadata_lock);
        return 0;
    }
    if (vfs_metadata_table[idx].has_file_caps) {
        required += sizeof("security.capability");
    }
    for (size_t i = 0; i < VFS_USER_XATTR_MAX; i++) {
        if (vfs_metadata_table[idx].user_xattrs[i].active) {
            required += strlen(vfs_metadata_table[idx].user_xattrs[i].name) + 1;
        }
    }
    if (list) {
        if (size < required) {
            fs_mutex_unlock(&vfs_metadata_lock);
            return -ERANGE;
        }
        if (vfs_metadata_table[idx].has_file_caps) {
            memcpy(list + pos, "security.capability", sizeof("security.capability"));
            pos += sizeof("security.capability");
        }
        for (size_t i = 0; i < VFS_USER_XATTR_MAX; i++) {
            if (vfs_metadata_table[idx].user_xattrs[i].active) {
                size_t name_len = strlen(vfs_metadata_table[idx].user_xattrs[i].name) + 1;
                memcpy(list + pos, vfs_metadata_table[idx].user_xattrs[i].name, name_len);
                pos += name_len;
            }
        }
    }
    fs_mutex_unlock(&vfs_metadata_lock);
    return (long)required;
}

long vfs_list_xattr(const char *path, char *list, size_t size) {
    return vfs_list_xattr_follow(path, list, size, 1);
}

int vfs_remove_user_xattr_follow(const char *path, const char *name, int follow_final_symlink) {
    char resolved[MAX_PATH];
    int idx;
    int xidx;
    uint64_t identity;
    int ret;

    if (!name) {
        return -EFAULT;
    }
    if (strncmp(name, "user.", 5) != 0) {
        return -ENODATA;
    }
    ret = vfs_resolve_existing_metadata_path_follow(path, resolved, sizeof(resolved),
                                                    follow_final_symlink);
    if (ret != 0) {
        return ret;
    }

    fs_mutex_lock(&vfs_metadata_lock);
    idx = vfs_metadata_find_locked(resolved);
    if (idx < 0) {
        fs_mutex_unlock(&vfs_metadata_lock);
        return -ENODATA;
    }
    xidx = vfs_user_xattr_find_locked(&vfs_metadata_table[idx], name);
    if (xidx < 0) {
        fs_mutex_unlock(&vfs_metadata_lock);
        return -ENODATA;
    }
    identity = vfs_metadata_table[idx].file_identity;
    vfs_user_xattr_remove_identity_locked(identity, name);
    fs_mutex_unlock(&vfs_metadata_lock);
    return 0;
}

int vfs_remove_user_xattr(const char *path, const char *name) {
    return vfs_remove_user_xattr_follow(path, name, 1);
}

int vfs_set_file_capabilities_follow(const char *path, uint64_t permitted, uint64_t inheritable,
                                     bool effective, int follow_final_symlink) {
    char resolved[MAX_PATH];
    int idx;
    int free_slot = -1;
    int ret;

    ret = vfs_resolve_existing_metadata_path_follow(path, resolved, sizeof(resolved),
                                                    follow_final_symlink);
    if (ret != 0) {
        return ret;
    }
    if (!cred_has_cap(get_current_cred(), CAP_SETFCAP)) {
        return -EPERM;
    }

    fs_mutex_lock(&vfs_metadata_lock);
    idx = vfs_metadata_find_locked(resolved);
    if (idx < 0) {
        for (size_t i = 0; i < VFS_METADATA_MAX; i++) {
            if (!vfs_metadata_table[i].active) {
                free_slot = (int)i;
                break;
            }
        }
        idx = free_slot;
    }
    if (idx < 0) {
        fs_mutex_unlock(&vfs_metadata_lock);
        return -ENOSPC;
    }
    vfs_metadata_table[idx].active = true;
    vfs_copy_string(resolved, vfs_metadata_table[idx].path, sizeof(vfs_metadata_table[idx].path));
    if (vfs_metadata_table[idx].file_identity == 0) {
        vfs_metadata_table[idx].file_identity = (uint64_t)atomic_inc_return(&vfs_next_file_identity);
    }
    vfs_metadata_table[idx].has_file_caps = (permitted | inheritable) != 0 || effective;
    vfs_metadata_table[idx].cap_permitted = permitted;
    vfs_metadata_table[idx].cap_inheritable = inheritable;
    vfs_metadata_table[idx].cap_effective = effective;
    fs_mutex_unlock(&vfs_metadata_lock);
    return 0;
}

int vfs_set_file_capabilities(const char *path, uint64_t permitted, uint64_t inheritable,
                              bool effective) {
    return vfs_set_file_capabilities_follow(path, permitted, inheritable, effective, 1);
}

int vfs_get_file_capabilities_follow(const char *path, uint64_t *permitted,
                                     uint64_t *inheritable, bool *effective,
                                     int follow_final_symlink) {
    char resolved[MAX_PATH];
    int idx;
    int ret;

    if (!permitted || !inheritable || !effective) {
        return -EFAULT;
    }
    *permitted = 0;
    *inheritable = 0;
    *effective = false;

    ret = vfs_resolve_existing_metadata_path_follow(path, resolved, sizeof(resolved),
                                                    follow_final_symlink);
    if (ret != 0) {
        return ret;
    }

    fs_mutex_lock(&vfs_metadata_lock);
    idx = vfs_metadata_find_locked(resolved);
    if (idx < 0 || !vfs_metadata_table[idx].has_file_caps) {
        fs_mutex_unlock(&vfs_metadata_lock);
        return -ENODATA;
    }
    *permitted = vfs_metadata_table[idx].cap_permitted;
    *inheritable = vfs_metadata_table[idx].cap_inheritable;
    *effective = vfs_metadata_table[idx].cap_effective;
    fs_mutex_unlock(&vfs_metadata_lock);
    return 0;
}

int vfs_get_file_capabilities(const char *path, uint64_t *permitted, uint64_t *inheritable,
                              bool *effective) {
    return vfs_get_file_capabilities_follow(path, permitted, inheritable, effective, 1);
}

int vfs_remove_file_capabilities_follow(const char *path, int follow_final_symlink) {
    char resolved[MAX_PATH];
    int idx;
    int ret;

    ret = vfs_resolve_existing_metadata_path_follow(path, resolved, sizeof(resolved),
                                                    follow_final_symlink);
    if (ret != 0) {
        return ret;
    }
    if (!cred_has_cap(get_current_cred(), CAP_SETFCAP)) {
        return -EPERM;
    }

    fs_mutex_lock(&vfs_metadata_lock);
    idx = vfs_metadata_find_locked(resolved);
    if (idx < 0 || !vfs_metadata_table[idx].has_file_caps) {
        fs_mutex_unlock(&vfs_metadata_lock);
        return -ENODATA;
    }
    vfs_metadata_table[idx].has_file_caps = false;
    vfs_metadata_table[idx].cap_permitted = 0;
    vfs_metadata_table[idx].cap_inheritable = 0;
    vfs_metadata_table[idx].cap_effective = false;
    fs_mutex_unlock(&vfs_metadata_lock);
    return 0;
}

int vfs_remove_file_capabilities(const char *path) {
    return vfs_remove_file_capabilities_follow(path, 1);
}

void vfs_forget_path_metadata(const char *resolved_vpath) {
    int idx;

    if (!resolved_vpath) {
        return;
    }

    fs_mutex_lock(&vfs_metadata_lock);
    idx = vfs_metadata_find_locked(resolved_vpath);
    if (idx >= 0) {
        memset(&vfs_metadata_table[idx], 0, sizeof(vfs_metadata_table[idx]));
    }
    fs_mutex_unlock(&vfs_metadata_lock);
}

void vfs_link_path_metadata(const char *old_resolved_vpath, const char *new_resolved_vpath) {
    int old_idx;
    int new_idx;
    int free_slot = -1;
    uint64_t identity;

    if (!old_resolved_vpath || !new_resolved_vpath) {
        return;
    }

    fs_mutex_lock(&vfs_metadata_lock);
    old_idx = vfs_metadata_find_locked(old_resolved_vpath);
    if (old_idx < 0) {
        fs_mutex_unlock(&vfs_metadata_lock);
        identity = vfs_file_identity_for_path(old_resolved_vpath);
        fs_mutex_lock(&vfs_metadata_lock);
        old_idx = vfs_metadata_find_locked(old_resolved_vpath);
    } else {
        identity = vfs_metadata_table[old_idx].file_identity;
    }
    if (identity == 0) {
        identity = (uint64_t)atomic_inc_return(&vfs_next_file_identity);
        if (old_idx >= 0) {
            vfs_metadata_table[old_idx].file_identity = identity;
        }
    }

    new_idx = vfs_metadata_find_locked(new_resolved_vpath);
    if (new_idx < 0) {
        for (size_t i = 0; i < VFS_METADATA_MAX; i++) {
            if (!vfs_metadata_table[i].active) {
                free_slot = (int)i;
                break;
            }
        }
        new_idx = free_slot;
    }
    if (new_idx >= 0) {
        vfs_metadata_table[new_idx].active = true;
        vfs_copy_string(new_resolved_vpath, vfs_metadata_table[new_idx].path,
                        sizeof(vfs_metadata_table[new_idx].path));
        vfs_metadata_table[new_idx].file_identity = identity;
        if (old_idx >= 0) {
            vfs_metadata_table[new_idx].has_attrs = vfs_metadata_table[old_idx].has_attrs;
            vfs_metadata_table[new_idx].uid = vfs_metadata_table[old_idx].uid;
            vfs_metadata_table[new_idx].gid = vfs_metadata_table[old_idx].gid;
            vfs_metadata_table[new_idx].mode = vfs_metadata_table[old_idx].mode;
            vfs_metadata_table[new_idx].has_times = vfs_metadata_table[old_idx].has_times;
            vfs_metadata_table[new_idx].atime_sec = vfs_metadata_table[old_idx].atime_sec;
            vfs_metadata_table[new_idx].atime_nsec = vfs_metadata_table[old_idx].atime_nsec;
            vfs_metadata_table[new_idx].mtime_sec = vfs_metadata_table[old_idx].mtime_sec;
            vfs_metadata_table[new_idx].mtime_nsec = vfs_metadata_table[old_idx].mtime_nsec;
            vfs_metadata_table[new_idx].has_file_caps = vfs_metadata_table[old_idx].has_file_caps;
            vfs_metadata_table[new_idx].cap_permitted = vfs_metadata_table[old_idx].cap_permitted;
            vfs_metadata_table[new_idx].cap_inheritable = vfs_metadata_table[old_idx].cap_inheritable;
            vfs_metadata_table[new_idx].cap_effective = vfs_metadata_table[old_idx].cap_effective;
            memcpy(vfs_metadata_table[new_idx].user_xattrs, vfs_metadata_table[old_idx].user_xattrs,
                   sizeof(vfs_metadata_table[new_idx].user_xattrs));
        }
    }
    fs_mutex_unlock(&vfs_metadata_lock);
}

void vfs_rename_path_metadata(const char *old_resolved_vpath, const char *new_resolved_vpath) {
    int old_idx;
    int new_idx;

    if (!old_resolved_vpath || !new_resolved_vpath) {
        return;
    }

    fs_mutex_lock(&vfs_metadata_lock);
    old_idx = vfs_metadata_find_locked(old_resolved_vpath);
    if (old_idx < 0) {
        fs_mutex_unlock(&vfs_metadata_lock);
        return;
    }

    new_idx = vfs_metadata_find_locked(new_resolved_vpath);
    if (new_idx >= 0 && new_idx != old_idx) {
        memset(&vfs_metadata_table[new_idx], 0, sizeof(vfs_metadata_table[new_idx]));
    }
    vfs_copy_string(new_resolved_vpath, vfs_metadata_table[old_idx].path,
                    sizeof(vfs_metadata_table[old_idx].path));
    fs_mutex_unlock(&vfs_metadata_lock);
}

void vfs_exchange_path_metadata(const char *left_resolved_vpath, const char *right_resolved_vpath) {
    int left_idx;
    int right_idx;

    if (!left_resolved_vpath || !right_resolved_vpath || strcmp(left_resolved_vpath, right_resolved_vpath) == 0) {
        return;
    }

    fs_mutex_lock(&vfs_metadata_lock);
    left_idx = vfs_metadata_find_locked(left_resolved_vpath);
    right_idx = vfs_metadata_find_locked(right_resolved_vpath);

    if (left_idx >= 0 && right_idx >= 0) {
        struct vfs_metadata_entry left = vfs_metadata_table[left_idx];
        struct vfs_metadata_entry right = vfs_metadata_table[right_idx];

        vfs_metadata_table[left_idx] = right;
        vfs_copy_string(left_resolved_vpath, vfs_metadata_table[left_idx].path,
                        sizeof(vfs_metadata_table[left_idx].path));
        vfs_metadata_table[right_idx] = left;
        vfs_copy_string(right_resolved_vpath, vfs_metadata_table[right_idx].path,
                        sizeof(vfs_metadata_table[right_idx].path));
    } else if (left_idx >= 0) {
        vfs_copy_string(right_resolved_vpath, vfs_metadata_table[left_idx].path,
                        sizeof(vfs_metadata_table[left_idx].path));
    } else if (right_idx >= 0) {
        vfs_copy_string(left_resolved_vpath, vfs_metadata_table[right_idx].path,
                        sizeof(vfs_metadata_table[right_idx].path));
    }

    fs_mutex_unlock(&vfs_metadata_lock);
}

static bool vfs_cred_has_mode_permission(const struct cred *cred, const struct stat *st,
                                         uint32_t mask) {
    uint32_t perm;

    if (!cred || !st) {
        return false;
    }

    if (cred_has_cap(cred, CAP_DAC_OVERRIDE)) {
        if ((mask & 0111U) != 0 && (st->st_mode & 0111U) == 0) {
            return false;
        }
        return true;
    }
    if ((mask & 04U) != 0 && (mask & ~04U) == 0 && cred_has_cap(cred, CAP_DAC_READ_SEARCH)) {
        return true;
    }

    if (cred->fsuid == st->st_uid) {
        perm = (st->st_mode >> 6) & 07U;
    } else if (cred_has_group(cred, st->st_gid)) {
        perm = (st->st_mode >> 3) & 07U;
    } else {
        perm = st->st_mode & 07U;
    }

    return (perm & mask) == mask;
}

static bool vfs_cred_has_mode_permission_as(const struct cred *cred, const struct stat *st,
                                            uint32_t mask, uint32_t uid, uint32_t gid) {
    uint32_t perm;

    if (!cred || !st) {
        return false;
    }

    if (cred_has_cap(cred, CAP_DAC_OVERRIDE)) {
        if ((mask & 0111U) != 0 && (st->st_mode & 0111U) == 0) {
            return false;
        }
        return true;
    }
    if ((mask & 04U) != 0 && (mask & ~04U) == 0 && cred_has_cap(cred, CAP_DAC_READ_SEARCH)) {
        return true;
    }

    if (uid == st->st_uid) {
        perm = (st->st_mode >> 6) & 07U;
    } else if (gid == st->st_gid || cred_has_group(cred, st->st_gid)) {
        perm = (st->st_mode >> 3) & 07U;
    } else {
        perm = st->st_mode & 07U;
    }

    return (perm & mask) == mask;
}

int vfs_chmod_metadata(const char *resolved_vpath, uint32_t mode) {
    char translated_path[MAX_PATH];
    struct stat st;
    struct cred *cred = get_current_cred();
    uint64_t identity;
    int ret;

    if (!resolved_vpath || !cred) {
        return -EINVAL;
    }

    ret = vfs_translate_path(resolved_vpath, translated_path, sizeof(translated_path));
    if (ret != 0) {
        return ret;
    }
    ret = vfs_stat_virtual_backed_path(resolved_vpath, translated_path, &st);
    if (ret != 0) {
        return ret;
    }
    if (!cred_has_cap(cred, CAP_FOWNER) && cred->fsuid != st.st_uid) {
        return -EPERM;
    }

    st.st_mode = (st.st_mode & ~07777U) | (mode & 07777U);
    identity = vfs_file_identity_for_path(resolved_vpath);

    fs_mutex_lock(&vfs_metadata_lock);
    ret = vfs_record_metadata_for_stat(resolved_vpath, &st);
    if (ret == 0) {
        vfs_metadata_sync_identity_attrs_locked(identity, st.st_uid, st.st_gid, st.st_mode);
    }
    fs_mutex_unlock(&vfs_metadata_lock);
    return ret;
}

int vfs_chown_metadata(const char *resolved_vpath, uint32_t owner, uint32_t group) {
    char translated_path[MAX_PATH];
    struct stat st;
    struct cred *cred = get_current_cred();
    uint64_t identity;
    int ret;

    if (!resolved_vpath || !cred) {
        return -EINVAL;
    }
    if (!cred_has_cap(cred, CAP_CHOWN)) {
        return -EPERM;
    }

    ret = vfs_translate_path(resolved_vpath, translated_path, sizeof(translated_path));
    if (ret != 0) {
        return ret;
    }
    ret = vfs_stat_virtual_backed_path(resolved_vpath, translated_path, &st);
    if (ret != 0) {
        return ret;
    }

    if (owner != (uint32_t)-1) {
        st.st_uid = owner;
    }
    if (group != (uint32_t)-1) {
        st.st_gid = group;
    }
    identity = vfs_file_identity_for_path(resolved_vpath);

    fs_mutex_lock(&vfs_metadata_lock);
    ret = vfs_record_metadata_for_stat(resolved_vpath, &st);
    if (ret == 0) {
        vfs_metadata_sync_identity_attrs_locked(identity, st.st_uid, st.st_gid, st.st_mode);
    }
    fs_mutex_unlock(&vfs_metadata_lock);
    return ret;
}

int vfs_utimens_metadata(const char *resolved_vpath, long atime_sec, unsigned long atime_nsec,
                         long mtime_sec, unsigned long mtime_nsec) {
    char translated_path[MAX_PATH];
    struct stat st;
    struct cred *cred = get_current_cred();
    uint64_t identity;
    int ret;

    if (!resolved_vpath || !cred) {
        return -EINVAL;
    }
    if (atime_nsec >= 1000000000UL || mtime_nsec >= 1000000000UL ||
        atime_sec < 0 || mtime_sec < 0) {
        return -EINVAL;
    }

    ret = vfs_translate_path(resolved_vpath, translated_path, sizeof(translated_path));
    if (ret != 0) {
        return ret;
    }
    ret = vfs_stat_virtual_backed_path(resolved_vpath, translated_path, &st);
    if (ret != 0) {
        return ret;
    }
    if (!cred_has_cap(cred, CAP_FOWNER) && cred->fsuid != st.st_uid &&
        !vfs_cred_has_mode_permission(cred, &st, 02)) {
        return -EACCES;
    }

    identity = vfs_file_identity_for_path(resolved_vpath);

    fs_mutex_lock(&vfs_metadata_lock);
    ret = vfs_record_metadata_for_stat(resolved_vpath, &st);
    if (ret == 0) {
        vfs_metadata_sync_identity_times_locked(identity, atime_sec, atime_nsec,
                                               mtime_sec, mtime_nsec);
    }
    fs_mutex_unlock(&vfs_metadata_lock);
    return ret;
}

static int vfs_stat_virtual_backed_path(const char *resolved_vpath, const char *translated_path,
                                        struct stat *st) {
    if (!resolved_vpath || !translated_path || !st) {
        return -EINVAL;
    }

    {
        int ret = backing_stat(translated_path, st);

        if (ret != 0) {
            return ret;
        }
    }
    vfs_apply_stat_metadata(resolved_vpath, st);
    return 0;
}

static int vfs_parent_path(const char *resolved_vpath, char *parent, size_t parent_len) {
    const char *slash;
    size_t len;

    if (!resolved_vpath || !parent || parent_len == 0 || resolved_vpath[0] != '/') {
        return -EINVAL;
    }

    slash = strrchr(resolved_vpath, '/');
    if (!slash) {
        return -EINVAL;
    }
    if (slash == resolved_vpath) {
        return vfs_copy_string("/", parent, parent_len);
    }

    len = (size_t)(slash - resolved_vpath);
    if (len >= parent_len) {
        return -ENAMETOOLONG;
    }
    memcpy(parent, resolved_vpath, len);
    parent[len] = '\0';
    return 0;
}

int vfs_check_parent_mutation_permission(const char *resolved_vpath) {
    char parent[MAX_PATH];
    char translated_parent[MAX_PATH];
    struct stat st;
    struct cred *cred = get_current_cred();
    int ret;

    if (vfs_path_is_on_readonly_mount(resolved_vpath)) {
        return -EROFS;
    }

    ret = vfs_parent_path(resolved_vpath, parent, sizeof(parent));
    if (ret != 0) {
        return ret;
    }
    ret = vfs_translate_path(parent, translated_parent, sizeof(translated_parent));
    if (ret != 0) {
        return ret;
    }
    ret = vfs_stat_virtual_backed_path(parent, translated_parent, &st);
    if (ret != 0) {
        return ret;
    }
    if (!S_ISDIR(st.st_mode)) {
        return -ENOTDIR;
    }
    if (!vfs_cred_has_mode_permission(cred, &st, 03U)) {
        return -EACCES;
    }

    return 0;
}

int vfs_check_sticky_unlink_permission(const char *resolved_vpath) {
    char parent[MAX_PATH];
    char translated_parent[MAX_PATH];
    char translated_target[MAX_PATH];
    struct stat parent_st;
    struct stat target_st;
    struct cred *cred = get_current_cred();
    int ret;

    if (!resolved_vpath || !cred) {
        return -EINVAL;
    }

    ret = vfs_parent_path(resolved_vpath, parent, sizeof(parent));
    if (ret != 0) {
        return ret;
    }
    ret = vfs_translate_path(parent, translated_parent, sizeof(translated_parent));
    if (ret != 0) {
        return ret;
    }
    ret = vfs_stat_virtual_backed_path(parent, translated_parent, &parent_st);
    if (ret != 0) {
        return ret;
    }
    if ((parent_st.st_mode & S_ISVTX) == 0) {
        return 0;
    }
    if (cred_has_cap(cred, CAP_FOWNER) || cred->fsuid == parent_st.st_uid) {
        return 0;
    }

    ret = vfs_translate_path(resolved_vpath, translated_target, sizeof(translated_target));
    if (ret != 0) {
        return ret;
    }
    ret = vfs_lstat(translated_target, &target_st);
    if (ret != 0) {
        return ret;
    }
    vfs_apply_stat_metadata(resolved_vpath, &target_st);
    if (cred->fsuid == target_st.st_uid) {
        return 0;
    }

    return -EPERM;
}

int vfs_check_sticky_rename_permission(const char *old_resolved_vpath, const char *new_resolved_vpath) {
    int ret;

    ret = vfs_check_sticky_unlink_permission(old_resolved_vpath);
    if (ret != 0) {
        return ret;
    }

    ret = vfs_check_sticky_unlink_permission(new_resolved_vpath);
    if (ret == -ENOENT) {
        return 0;
    }
    return ret;
}

static int vfs_check_search_permission(const char *resolved_vpath) {
    char current[MAX_PATH];
    struct cred *cred = get_current_cred();
    size_t len;

    if (!resolved_vpath || resolved_vpath[0] != '/') {
        return -EINVAL;
    }
    if (!cred) {
        return -ESRCH;
    }

    len = strlen(resolved_vpath);
    for (size_t i = 1; i < len; i++) {
        char translated[MAX_PATH];
        struct stat st;
        int ret;

        if (resolved_vpath[i] != '/') {
            continue;
        }
        if (i >= sizeof(current)) {
            return -ENAMETOOLONG;
        }
        memcpy(current, resolved_vpath, i);
        current[i] = '\0';
        if (strcmp(current, "/") == 0) {
            continue;
        }
        ret = vfs_translate_path(current, translated, sizeof(translated));
        if (ret != 0) {
            return ret;
        }
        ret = vfs_stat_virtual_backed_path(current, translated, &st);
        if (ret != 0) {
            return ret;
        }
        if (!S_ISDIR(st.st_mode)) {
            return -ENOTDIR;
        }
        if (!vfs_cred_has_mode_permission(cred, &st, 01U)) {
            return -EACCES;
        }
    }

    return 0;
}

int vfs_check_open_permission(const char *resolved_vpath, const char *translated_path, int flags) {
    struct stat st;
    struct cred *cred = get_current_cred();
    int access_mode = flags & O_ACCMODE;
    bool wants_read = access_mode == O_RDONLY || access_mode == O_RDWR;
    bool wants_write = access_mode == O_WRONLY || access_mode == O_RDWR;
    bool exists;
    int ret;

    if (!resolved_vpath || !translated_path) {
        return -EINVAL;
    }

    if ((wants_write || (flags & O_TRUNC) != 0) && vfs_path_is_on_readonly_mount(resolved_vpath)) {
        return -EROFS;
    }

    if ((flags & O_NOFOLLOW) != 0) {
        ret = vfs_lstat(translated_path, &st);
        if (ret == 0 && S_ISLNK(st.st_mode)) {
            return -ELOOP;
        }
        if (ret != 0 && ret != -ENOENT) {
            return ret;
        }
    }

    ret = vfs_stat_virtual_backed_path(resolved_vpath, translated_path, &st);
    exists = ret == 0;
    if (!exists && ret != -ENOENT) {
        return ret;
    }

    if (!exists) {
        if (!wants_write) {
            return -ENOENT;
        }
        return vfs_check_parent_mutation_permission(resolved_vpath);
    }

    ret = vfs_check_search_permission(resolved_vpath);
    if (ret != 0) {
        return ret;
    }

    if (S_ISDIR(st.st_mode) && wants_write) {
        return -EISDIR;
    }
    if (wants_read && !vfs_cred_has_mode_permission(cred, &st, 04U)) {
        return -EACCES;
    }
    if (wants_write && !vfs_cred_has_mode_permission(cred, &st, 02U)) {
        return -EACCES;
    }
    if ((flags & O_TRUNC) != 0 && !vfs_cred_has_mode_permission(cred, &st, 02U)) {
        return -EACCES;
    }

    return 0;
}



int vfs_normalize_linux_path(const char *input, char *output, size_t output_len) {
    char scratch[MAX_PATH];
    size_t input_len;
    size_t normalized_len;

    if (!input || !output || output_len == 0) {
        return -EINVAL;
    }

    input_len = strlen(input);
    if (input_len == 0) {
        return -ENOENT;
    }

    if (input_len >= sizeof(scratch)) {
        return -ENAMETOOLONG;
    }

    if (strcmp(input, ".") == 0 || strcmp(input, "..") == 0 || strstr(input, "/../") != NULL ||
        strncmp(input, "../", 3) == 0 ||
        (input_len >= 3 && strcmp(input + input_len - 3, "/..") == 0)) {
        return -EINVAL;
    }

    if (input[0] == '/') {
        memcpy(scratch, input, input_len + 1);
    } else {
        if (input_len + 2 > sizeof(scratch)) {
            return -ENAMETOOLONG;
        }
        scratch[0] = '/';
        memcpy(scratch + 1, input, input_len + 1);
    }

    while (strstr(scratch, "//") != NULL) {
        char *double_slash = strstr(scratch, "//");
        memmove(double_slash, double_slash + 1, strlen(double_slash));
    }

    if (strstr(scratch, "/./") != NULL) {
        return -EINVAL;
    }

    normalized_len = strlen(scratch);
    if (normalized_len > 1 && scratch[normalized_len - 1] == '/') {
        scratch[normalized_len - 1] = '\0';
    }

    return vfs_copy_string(scratch, output, output_len);
}

static bool vfs_path_matches_prefix(const char *vpath, const char *prefix) {
    size_t prefklen;

    if (!vpath || !prefix) {
        return false;
    }

    if (strcmp(prefix, "/") == 0) {
        return vpath[0] == '/';
    }

    prefklen = strlen(prefix);
    if (strncmp(vpath, prefix, prefklen) != 0) {
        return false;
    }

    return vpath[prefklen] == '\0' || vpath[prefklen] == '/';
}

static const struct vfs_route_entry *vfs_route_for_path(const char *vpath) {
    const struct vfs_route_entry *best_match = NULL;
    size_t best_len = 0;
    size_t i;

    if (!vpath || vpath[0] != '/') {
        return NULL;
    }

    for (i = 0; i < vfs_route_table_count; i++) {
        const struct vfs_route_entry *route = &vfs_route_table[i];
        size_t prefklen = strlen(route->linux_prefix);

        if (!vfs_path_matches_prefix(vpath, route->linux_prefix)) {
            continue;
        }

        if (prefklen > best_len) {
            best_match = route;
            best_len = prefklen;
        }
    }

    return best_match;
}

static const char *vfs_relative_suffixfor_route(const struct vfs_route_entry *route,
                                                 const char *normalized_virtual_path) {
    if (!route || !normalized_virtual_path) {
        return NULL;
    }

    if (!route->strip_linux_prefix) {
        return normalized_virtual_path;
    }

    return normalized_virtual_path + strlen(route->linux_prefix);
}

static int vfs_rewrite_mount_path_locked(const char *normalized_virtual_path, char *mounted_path,
                                         struct vfs_mount_namespace *mnt_ns,
                                         size_t mounted_path_len) {
    const struct vfs_mount_entry *best = NULL;
    size_t best_len = 0;

    if (!normalized_virtual_path || !mounted_path || mounted_path_len == 0) {
        return -EINVAL;
    }

    for (size_t i = 0; i < MAX_MOUNTS; i++) {
        const struct vfs_mount_entry *entry = &mnt_ns->entries[i];
        size_t target_len;

        if (!entry->active || !vfs_path_matches_prefix(normalized_virtual_path, entry->target)) {
            continue;
        }

        target_len = strlen(entry->target);
        if (!best || target_len > best_len) {
            best = entry;
            best_len = target_len;
        }
    }

    if (!best) {
        return vfs_copy_string(normalized_virtual_path, mounted_path, mounted_path_len);
    }

    if (strcmp(normalized_virtual_path, best->target) == 0) {
        return vfs_copy_string(best->source, mounted_path, mounted_path_len);
    }

    return vfs_join_virtual_path(best->source, normalized_virtual_path + best_len, mounted_path,
                                 mounted_path_len);
}

static int vfs_apply_mounts(const char *normalized_virtual_path, char *mounted_path,
                            size_t mounted_path_len) {
    int ret;
    struct vfs_mount_namespace *mnt_ns = vfs_task_mount_namespace();

    if (!mnt_ns) {
        return vfs_copy_string(normalized_virtual_path, mounted_path, mounted_path_len);
    }

    fs_mutex_lock(&mnt_ns->lock);
    ret = vfs_rewrite_mount_path_locked(normalized_virtual_path, mounted_path, mnt_ns, mounted_path_len);
    fs_mutex_unlock(&mnt_ns->lock);

    return ret;
}

int vfs_apply_mounts_to_path(const char *normalized_virtual_path, char *mounted_path,
                             size_t mounted_path_len) {
    if (!normalized_virtual_path || !mounted_path || mounted_path_len == 0) {
        return -EINVAL;
    }
    return vfs_apply_mounts(normalized_virtual_path, mounted_path, mounted_path_len);
}

static const struct vfs_mount_entry *vfs_find_mount_for_path_locked(const char *normalized_virtual_path,
                                                                    struct vfs_mount_namespace *mnt_ns) {
    const struct vfs_mount_entry *best = NULL;
    size_t best_len = 0;

    if (!normalized_virtual_path || !mnt_ns) {
        return NULL;
    }

    for (size_t i = 0; i < MAX_MOUNTS; i++) {
        const struct vfs_mount_entry *entry = &mnt_ns->entries[i];
        size_t target_len;

        if (!entry->active || !vfs_path_matches_prefix(normalized_virtual_path, entry->target)) {
            continue;
        }

        target_len = strlen(entry->target);
        if (!best || target_len > best_len) {
            best = entry;
            best_len = target_len;
        }
    }

    return best;
}

static bool vfs_path_is_on_readonly_mount(const char *resolved_vpath) {
    struct vfs_mount_namespace *mnt_ns = vfs_task_mount_namespace();
    const struct vfs_mount_entry *entry;
    bool readonly = false;

    if (!resolved_vpath || !mnt_ns) {
        return false;
    }

    fs_mutex_lock(&mnt_ns->lock);
    entry = vfs_find_mount_for_path_locked(resolved_vpath, mnt_ns);
    readonly = entry && ((entry->flags & MNT_READONLY) != 0);
    fs_mutex_unlock(&mnt_ns->lock);

    return readonly;
}

unsigned long vfs_mount_flags_for_path(const char *resolved_vpath) {
    struct vfs_mount_namespace *mnt_ns = vfs_task_mount_namespace();
    const struct vfs_mount_entry *entry;
    unsigned long flags = 0;

    if (!resolved_vpath || !mnt_ns) {
        return 0;
    }

    fs_mutex_lock(&mnt_ns->lock);
    entry = vfs_find_mount_for_path_locked(resolved_vpath, mnt_ns);
    flags = entry ? entry->flags : 0;
    fs_mutex_unlock(&mnt_ns->lock);

    return flags;
}

static uint64_t vfs_mount_id_for_index_locked(const struct vfs_mount_namespace *mnt_ns, size_t target_index) {
    if (!mnt_ns || target_index >= MAX_MOUNTS || !mnt_ns->entries[target_index].active) {
        return 0;
    }
    return mnt_ns->entries[target_index].mount_id;
}

static const struct vfs_mount_entry *vfs_mount_for_id_locked(const struct vfs_mount_namespace *mnt_ns,
                                                             uint64_t id,
                                                             size_t *index_out) {
    if (!mnt_ns || id < 2) {
        return NULL;
    }
    for (size_t i = 0; i < MAX_MOUNTS; i++) {
        if (!mnt_ns->entries[i].active) {
            continue;
        }
        if (mnt_ns->entries[i].mount_id == id) {
            if (index_out) {
                *index_out = i;
            }
            return &mnt_ns->entries[i];
        }
    }
    return NULL;
}

static uint64_t vfs_mount_parent_id_locked(const struct vfs_mount_namespace *mnt_ns, size_t target_index) {
    const struct vfs_mount_entry *entry;
    uint64_t parent_id = 1;
    size_t parent_len = 0;

    if (!mnt_ns || target_index >= MAX_MOUNTS || !mnt_ns->entries[target_index].active) {
        return 1;
    }
    entry = &mnt_ns->entries[target_index];
    for (size_t i = 0; i < MAX_MOUNTS; i++) {
        const struct vfs_mount_entry *candidate = &mnt_ns->entries[i];
        size_t candidate_len;

        if (i == target_index || !candidate->active || candidate->mount_id == 0 ||
            !vfs_path_matches_prefix(entry->target, candidate->target) ||
            strcmp(entry->target, candidate->target) == 0) {
            continue;
        }
        candidate_len = strlen(candidate->target);
        if (candidate_len > parent_len) {
            parent_len = candidate_len;
            parent_id = candidate->mount_id;
        }
    }
    return parent_id;
}

static bool vfs_mount_has_id_locked(const struct vfs_mount_namespace *mnt_ns, uint64_t id) {
    size_t ignored_index;

    if (id == LSMT_ROOT || id == 1) {
        return true;
    }
    return vfs_mount_for_id_locked(mnt_ns, id, &ignored_index) != NULL;
}

static int vfs_statmount_store_string(struct statmount *buf, size_t bufsize,
                                      size_t *str_pos, __u32 *offset_out,
                                      const char *value) {
    size_t base = sizeof(*buf);
    size_t len;

    if (!buf || !str_pos || !offset_out || !value || bufsize < base) {
        return -EINVAL;
    }
    len = strlen(value) + 1;
    if (*str_pos > U32_MAX || len > bufsize - base || *str_pos > (bufsize - base) - len) {
        return -EOVERFLOW;
    }
    *offset_out = (__u32)*str_pos;
    memcpy(buf->str + *str_pos, value, len);
    *str_pos += len;
    return 0;
}

int vfs_mount_setattr(int dirfd, const char *pathname, unsigned int flags,
                      const struct mount_attr *attr, size_t size) {
    char resolved_target[MAX_PATH];
    struct vfs_mount_namespace *mnt_ns = vfs_task_mount_namespace();
    uint64_t supported_attrs = MOUNT_ATTR_RDONLY | MOUNT_ATTR_NOSUID | MOUNT_ATTR_NODEV | MOUNT_ATTR_NOEXEC;
    int recursive = (flags & AT_RECURSIVE) != 0;
    int found = 0;
    int ret;

    if (!pathname || !attr) {
        return -EFAULT;
    }
    if (!mnt_ns) {
        return -ESRCH;
    }
    if (!vfs_current_has_mount_admin(mnt_ns)) {
        return -EPERM;
    }
    if (size < MOUNT_ATTR_SIZE_VER0 || (flags & ~AT_RECURSIVE) != 0) {
        return -EINVAL;
    }
    if ((attr->attr_set & ~supported_attrs) != 0 ||
        (attr->attr_clr & ~supported_attrs) != 0 ||
        (attr->attr_set & attr->attr_clr) != 0 ||
        vfs_mount_selected_propagation((unsigned long)attr->propagation) == (unsigned long)-1) {
        return -EINVAL;
    }

    ret = vfs_resolve_virtual_path_at(dirfd, pathname, resolved_target, sizeof(resolved_target));
    if (ret != 0) {
        return ret;
    }

    fs_mutex_lock(&mnt_ns->lock);
    for (size_t i = 0; i < MAX_MOUNTS; i++) {
        struct vfs_mount_entry *entry = &mnt_ns->entries[i];

        if (!entry->active ||
            (recursive ? !vfs_mount_target_matches_tree(entry->target, resolved_target)
                       : strcmp(entry->target, resolved_target) != 0)) {
            continue;
        }
        if ((attr->attr_set & MOUNT_ATTR_RDONLY) != 0) {
            entry->flags |= MNT_READONLY;
        }
        if ((attr->attr_clr & MOUNT_ATTR_RDONLY) != 0) {
            entry->flags &= ~MNT_READONLY;
        }
        if ((attr->attr_set & MOUNT_ATTR_NOSUID) != 0) {
            entry->flags |= MNT_NOSUID;
        }
        if ((attr->attr_clr & MOUNT_ATTR_NOSUID) != 0) {
            entry->flags &= ~MNT_NOSUID;
        }
        if ((attr->attr_set & MOUNT_ATTR_NODEV) != 0) {
            entry->flags |= MNT_NODEV;
        }
        if ((attr->attr_clr & MOUNT_ATTR_NODEV) != 0) {
            entry->flags &= ~MNT_NODEV;
        }
        if ((attr->attr_set & MOUNT_ATTR_NOEXEC) != 0) {
            entry->flags |= MNT_NOEXEC;
        }
        if ((attr->attr_clr & MOUNT_ATTR_NOEXEC) != 0) {
            entry->flags &= ~MNT_NOEXEC;
        }
        if (attr->propagation != 0) {
            vfs_mount_set_propagation_locked(mnt_ns, entry, (unsigned long)attr->propagation);
        }
        found = 1;
    }
    fs_mutex_unlock(&mnt_ns->lock);
    return found ? 0 : -EINVAL;
}

int vfs_open_tree(int dirfd, const char *pathname, unsigned int flags) {
    char resolved_target[MAX_PATH];
    struct vfs_mount_namespace *mnt_ns = vfs_task_mount_namespace();
    struct vfs_mount_fd mount_fd;
    int fd = -1;
    int ret;

    if (!pathname) {
        return -EFAULT;
    }
    if (!mnt_ns) {
        return -ESRCH;
    }
    if (!vfs_current_has_mount_admin(mnt_ns)) {
        return -EPERM;
    }
    if ((flags & ~(OPEN_TREE_CLONE | OPEN_TREE_CLOEXEC)) != 0) {
        return -EINVAL;
    }

    ret = vfs_resolve_virtual_path_at(dirfd, pathname, resolved_target, sizeof(resolved_target));
    if (ret != 0) {
        return ret;
    }

    memset(&mount_fd, 0, sizeof(mount_fd));
    fs_mutex_lock(&mnt_ns->lock);
    {
        const struct vfs_mount_entry *root = vfs_find_mount_for_path_locked(resolved_target, mnt_ns);
        size_t root_len = strlen(resolved_target);

        if (!root || strcmp(root->target, resolved_target) != 0) {
            fs_mutex_unlock(&mnt_ns->lock);
            return -EINVAL;
        }
        for (size_t i = 0; i < MAX_MOUNTS; i++) {
            const struct vfs_mount_entry *entry = &mnt_ns->entries[i];
            struct vfs_mount_fd_entry *snapshot;

            if (!entry->active || !vfs_mount_target_matches_tree(entry->target, resolved_target)) {
                continue;
            }
            if (mount_fd.entry_count >= VFS_MOUNT_FD_MAX_ENTRIES) {
                fs_mutex_unlock(&mnt_ns->lock);
                return -ENOSPC;
            }
            snapshot = &mount_fd.entries[mount_fd.entry_count++];
            ret = vfs_copy_string(entry->source, snapshot->source, sizeof(snapshot->source));
            if (ret == 0 && entry == root) {
                ret = vfs_copy_string("/", snapshot->target, sizeof(snapshot->target));
            } else if (ret == 0) {
                ret = vfs_copy_string(entry->target + root_len, snapshot->target,
                                      sizeof(snapshot->target));
            }
            if (ret == 0) {
                ret = vfs_copy_string(entry->fstype, snapshot->fstype, sizeof(snapshot->fstype));
            }
            snapshot->flags = entry->flags;
            snapshot->propagation = entry->propagation;
            if (ret != 0) {
                break;
            }
        }
    }
    fs_mutex_unlock(&mnt_ns->lock);
    if (ret != 0) {
        return ret;
    }

    fd = alloc_fd_impl();
    if (fd < 0) {
        return fd;
    }
    if (init_mount_fd_entry_impl(fd, O_RDONLY | ((flags & OPEN_TREE_CLOEXEC) ? O_CLOEXEC : 0),
                                 &mount_fd) != 0) {
        ret = -ENOMEM;
        free_fd_impl(fd);
        return ret;
    }
    return fd;
}

int vfs_move_mount(int from_dirfd, const char *from_pathname, int to_dirfd,
                   const char *to_pathname, unsigned int flags) {
    char resolved_from[MAX_PATH];
    char resolved_target[MAX_PATH];
    char backing_target[MAX_PATH];
    struct stat target_stat;
    struct vfs_mount_namespace *mnt_ns = vfs_task_mount_namespace();
    int ret;

    if (!from_pathname || !to_pathname) {
        return -EFAULT;
    }
    if (!mnt_ns) {
        return -ESRCH;
    }
    if (!vfs_current_has_mount_admin(mnt_ns)) {
        return -EPERM;
    }
    if ((flags & ~MOVE_MOUNT__MASK) != 0 ||
        (flags & (MOVE_MOUNT_SET_GROUP | MOVE_MOUNT_BENEATH)) != 0) {
        return -EINVAL;
    }

    ret = vfs_resolve_virtual_path_at(to_dirfd, to_pathname, resolved_target, sizeof(resolved_target));
    if (ret != 0) {
        return ret;
    }
    if (strcmp(resolved_target, "/") == 0) {
        return -EINVAL;
    }
    ret = vfs_translate_path(resolved_target, backing_target, sizeof(backing_target));
    if (ret != 0) {
        return ret;
    }
    ret = backing_stat(backing_target, &target_stat);
    if (ret != 0) {
        return ret;
    }
    if (!S_ISDIR(target_stat.st_mode)) {
        return -ENOTDIR;
    }

    if ((flags & MOVE_MOUNT_F_EMPTY_PATH) != 0) {
        void *entry;
        struct vfs_mount_fd mount_fd;
        int slot = -1;
        int added_slots[VFS_MOUNT_FD_MAX_ENTRIES];
        size_t added_count = 0;

        if (from_pathname[0] != '\0') {
            return -EINVAL;
        }
        entry = get_fd_entry_impl(from_dirfd);
        if (!entry) {
            return -EBADF;
        }
        if (!get_fd_is_mount_impl(entry) || get_fd_mount_impl(entry, &mount_fd) != 0) {
            put_fd_entry_impl(entry);
            return -EINVAL;
        }
        put_fd_entry_impl(entry);

        fs_mutex_lock(&mnt_ns->lock);
        for (size_t scan = 0; scan < mount_fd.entry_count; scan++) {
            char attached_target[MAX_PATH];
            const char *suffix = strcmp(mount_fd.entries[scan].target, "/") == 0 ?
                                 "" : mount_fd.entries[scan].target;
            ret = snprintf(attached_target, sizeof(attached_target), "%s%s", resolved_target, suffix);
            if (ret < 0 || (size_t)ret >= sizeof(attached_target)) {
                fs_mutex_unlock(&mnt_ns->lock);
                return -ENAMETOOLONG;
            }
            for (size_t i = 0; i < MAX_MOUNTS; i++) {
                if (mnt_ns->entries[i].active && strcmp(mnt_ns->entries[i].target, attached_target) == 0) {
                    fs_mutex_unlock(&mnt_ns->lock);
                    return -EBUSY;
                }
            }
        }
        for (size_t scan = 0; scan < mount_fd.entry_count; scan++) {
            char attached_target[MAX_PATH];
            const char *suffix = strcmp(mount_fd.entries[scan].target, "/") == 0 ?
                                 "" : mount_fd.entries[scan].target;
            ret = snprintf(attached_target, sizeof(attached_target), "%s%s", resolved_target, suffix);
            if (ret < 0 || (size_t)ret >= sizeof(attached_target)) {
                ret = -ENAMETOOLONG;
                break;
            }
            slot = vfs_mount_find_free_slot_locked(mnt_ns);
            if (slot < 0) {
                ret = -ENOSPC;
                break;
            }
            ret = vfs_mount_copy_entry(&mnt_ns->entries[slot], mount_fd.entries[scan].source,
                                       attached_target, mount_fd.entries[scan].fstype,
                                       mount_fd.entries[scan].flags,
                                       mount_fd.entries[scan].propagation);
            if (ret != 0) {
                break;
            }
            vfs_mount_assign_propagation_ids_locked(mnt_ns, &mnt_ns->entries[slot]);
            added_slots[added_count++] = slot;
        }
        if (ret == 0) {
            ret = vfs_mount_propagate_attached_subtree_locked(mnt_ns, added_slots, added_count);
        }
        if (ret != 0) {
            for (size_t i = 0; i < added_count; i++) {
                memset(&mnt_ns->entries[added_slots[i]], 0, sizeof(mnt_ns->entries[added_slots[i]]));
            }
        }
        fs_mutex_unlock(&mnt_ns->lock);
        return ret;
    }

    if (flags != 0) {
        return -EINVAL;
    }
    ret = vfs_resolve_virtual_path_at(from_dirfd, from_pathname, resolved_from, sizeof(resolved_from));
    if (ret != 0) {
        return ret;
    }
    fs_mutex_lock(&mnt_ns->lock);
    ret = vfs_mount_move_tree_with_propagation_locked(mnt_ns, resolved_from, resolved_target);
    fs_mutex_unlock(&mnt_ns->lock);
    return ret;
}

int vfs_pivot_root(const char *new_root, const char *put_old) {
    struct task *task = task_current();
    struct vfs_mount_namespace *mnt_ns = NULL;
    char resolved_new_root[MAX_PATH];
    char resolved_put_old[MAX_PATH];
    char backing_new_root[MAX_PATH];
    char backing_put_old[MAX_PATH];
    char old_pwd[MAX_PATH];
    struct stat st;
    int ret;

    if (!new_root || !put_old) {
        return -EFAULT;
    }
    if (new_root[0] == '\0' || put_old[0] == '\0') {
        return -ENOENT;
    }
    if (!task || !task->fs) {
        return -ESRCH;
    }
    mnt_ns = task->fs->mnt_ns;
    if (!vfs_current_has_mount_admin(mnt_ns)) {
        return -EPERM;
    }

    ret = vfs_resolve_virtual_path_at(AT_FDCWD, new_root, resolved_new_root, sizeof(resolved_new_root));
    if (ret != 0) {
        return ret;
    }
    ret = vfs_resolve_virtual_path_at(AT_FDCWD, put_old, resolved_put_old, sizeof(resolved_put_old));
    if (ret != 0) {
        return ret;
    }
    if (strcmp(resolved_new_root, "/") == 0 ||
        strcmp(resolved_new_root, resolved_put_old) == 0 ||
        !vfs_path_matches_prefix(resolved_put_old, resolved_new_root)) {
        return -EINVAL;
    }

    ret = vfs_translate_path(resolved_new_root, backing_new_root, sizeof(backing_new_root));
    if (ret != 0) {
        return ret;
    }
    ret = vfs_translate_path(resolved_put_old, backing_put_old, sizeof(backing_put_old));
    if (ret != 0) {
        return ret;
    }
    ret = backing_stat(backing_new_root, &st);
    if (ret != 0) {
        return ret;
    }
    if (!S_ISDIR(st.st_mode)) {
        return -ENOTDIR;
    }
    ret = backing_stat(backing_put_old, &st);
    if (ret != 0) {
        return ret;
    }
    if (!S_ISDIR(st.st_mode)) {
        return -ENOTDIR;
    }

    ret = vfs_mount("/", resolved_put_old, NULL, MS_BIND, NULL);
    if (ret != 0) {
        return ret;
    }

    memcpy(old_pwd, task->fs->pwd_path, sizeof(old_pwd));
    ret = fs_set_root(task->fs, resolved_new_root);
    if (ret != 0) {
        vfs_umount(resolved_put_old);
        return ret;
    }
    if (!vfs_path_matches_prefix(old_pwd, resolved_new_root)) {
        ret = fs_set_pwd(task->fs, resolved_new_root);
        if (ret != 0) {
            fs_set_root(task->fs, "/");
            vfs_umount(resolved_put_old);
            return ret;
        }
    }
    return 0;
}

long vfs_listmount(const struct mnt_id_req *req, uint64_t *mnt_ids, size_t nr_mnt_ids,
                   unsigned int flags) {
    struct vfs_mount_namespace *mnt_ns = vfs_task_mount_namespace();
    uint64_t count = 0;
    uint64_t parent_id;
    uint64_t cursor_id;
    bool cursor_seen;

    if (!req || (!mnt_ids && nr_mnt_ids > 0)) {
        return -EFAULT;
    }
    if (!mnt_ns) {
        return -ESRCH;
    }
    if (!vfs_current_has_mount_admin(mnt_ns)) {
        return -EPERM;
    }
    if (req->size < MNT_ID_REQ_SIZE_VER0 || (flags & ~LISTMOUNT_REVERSE) != 0) {
        return -EINVAL;
    }

    parent_id = req->mnt_id == LSMT_ROOT ? 1 : req->mnt_id;
    cursor_id = req->size >= MNT_ID_REQ_SIZE_VER1 ? req->param : 0;
    cursor_seen = cursor_id == 0;

    fs_mutex_lock(&mnt_ns->lock);
    if (!vfs_mount_has_id_locked(mnt_ns, parent_id)) {
        fs_mutex_unlock(&mnt_ns->lock);
        return -ENOENT;
    }
    if (cursor_id != 0 && !vfs_mount_has_id_locked(mnt_ns, cursor_id)) {
        fs_mutex_unlock(&mnt_ns->lock);
        return -ENOENT;
    }

    for (size_t scan = 0; scan < MAX_MOUNTS; scan++) {
        size_t i = (flags & LISTMOUNT_REVERSE) != 0 ? (MAX_MOUNTS - 1 - scan) : scan;
        uint64_t id;

        if (!mnt_ns->entries[i].active) {
            continue;
        }
        id = vfs_mount_id_for_index_locked(mnt_ns, i);
        if (vfs_mount_parent_id_locked(mnt_ns, i) != parent_id) {
            continue;
        }
        if (!cursor_seen) {
            if (id == cursor_id) {
                cursor_seen = true;
            }
            continue;
        }
        if (count < nr_mnt_ids) {
            mnt_ids[count] = id;
        }
        count++;
    }
    fs_mutex_unlock(&mnt_ns->lock);
    return (long)count;
}

int vfs_statmount(const struct mnt_id_req *req, struct statmount *buf, size_t bufsize,
                  unsigned int flags) {
    struct vfs_mount_namespace *mnt_ns = vfs_task_mount_namespace();
    const struct vfs_mount_entry *entry;
    size_t index = 0;
    size_t str_pos = 0;
    int ret;

    if (!req || !buf) {
        return -EFAULT;
    }
    if (!mnt_ns) {
        return -ESRCH;
    }
    if (!vfs_current_has_mount_admin(mnt_ns)) {
        return -EPERM;
    }
    if (req->size < MNT_ID_REQ_SIZE_VER0 || bufsize < sizeof(*buf) || flags != 0) {
        return -EINVAL;
    }

    memset(buf, 0, bufsize);
    fs_mutex_lock(&mnt_ns->lock);
    entry = vfs_mount_for_id_locked(mnt_ns, req->mnt_id, &index);
    if (!entry) {
        fs_mutex_unlock(&mnt_ns->lock);
        return -ENOENT;
    }

    buf->mask = req->param;
    buf->mnt_id = req->mnt_id;
    buf->mnt_id_old = (__u32)req->mnt_id;
    buf->mnt_parent_id = vfs_mount_parent_id_locked(mnt_ns, index);
    buf->mnt_parent_id_old = (__u32)buf->mnt_parent_id;
    buf->mnt_attr = 0;
    if ((entry->flags & MNT_READONLY) != 0) {
        buf->mnt_attr |= MOUNT_ATTR_RDONLY;
    }
    if ((entry->flags & MNT_NOSUID) != 0) {
        buf->mnt_attr |= MOUNT_ATTR_NOSUID;
    }
    if ((entry->flags & MNT_NODEV) != 0) {
        buf->mnt_attr |= MOUNT_ATTR_NODEV;
    }
    if ((entry->flags & MNT_NOEXEC) != 0) {
        buf->mnt_attr |= MOUNT_ATTR_NOEXEC;
    }
    buf->mnt_propagation = entry->propagation;
    buf->mnt_peer_group = entry->peer_group_id;
    buf->mnt_master = entry->master_group_id;
    buf->propagate_from = entry->master_group_id;
    buf->mnt_ns_id = mnt_ns->ns_id;

    ret = vfs_statmount_store_string(buf, bufsize, &str_pos, &buf->mnt_root, "/");
    if (ret == 0) {
        ret = vfs_statmount_store_string(buf, bufsize, &str_pos, &buf->mnt_point, entry->target);
    }
    if (ret != 0) {
        fs_mutex_unlock(&mnt_ns->lock);
        return ret;
    }
    (void)index;
    buf->size = (__u32)(sizeof(*buf) + str_pos);
    fs_mutex_unlock(&mnt_ns->lock);
    return 0;
}

int vfs_describe_route_for_path(const char *vpath, enum vfs_route_identity *route_id,
                                enum vfs_backing_class *backing_class, bool *reversible) {
    const struct vfs_route_entry *route = vfs_route_for_path(vpath);

    if (!route) {
        return -ENOENT;
    }

    if (route_id) {
        *route_id = route->route_id;
    }
    if (backing_class) {
        *backing_class = route->backing_class;
    }
    if (reversible) {
        *reversible = route->reverse_linux_prefix != NULL;
    }

    return 0;
}

bool vfs_path_is_linux_route(const char *vpath) {
    return vfs_route_for_path(vpath) != NULL;
}

bool vfs_path_is_synthetic(const char *vpath) {
    const struct vfs_route_entry *route = vfs_route_for_path(vpath);

    return route != NULL && route->synthetic;
}

bool vfs_path_is_synthetic_root(const char *vpath) {
    if (!vpath) {
        return false;
    }
    return strcmp(vpath, "/proc") == 0 || strcmp(vpath, "/sys") == 0 || strcmp(vpath, "/dev") == 0;
}

static bool vfs_path_is_synthetic_dev_dir(const char *vpath) {
    if (!vpath) {
        return false;
    }
    return strcmp(vpath, "/dev/pts") == 0;
}

synthetic_dev_node_t vfs_path_is_synthetic_dev_node(const char *vpath) {
    if (!vpath) {
        return SYNTHETIC_DEV_NONE;
    }
    if (strcmp(vpath, "/dev/null") == 0) {
        return SYNTHETIC_DEV_NULL;
    }
    if (strcmp(vpath, "/dev/zero") == 0) {
        return SYNTHETIC_DEV_ZERO;
    }
    if (strcmp(vpath, "/dev/random") == 0) {
        return SYNTHETIC_DEV_RANDOM;
    }
    if (strcmp(vpath, "/dev/urandom") == 0) {
        return SYNTHETIC_DEV_URANDOM;
    }
    if (strcmp(vpath, "/dev/ptmx") == 0) {
        return SYNTHETIC_DEV_PTMX;
    }
    return SYNTHETIC_DEV_NONE;
}

/* Determine backing class from virtual Linux path */
enum vfs_backing_class vfs_backing_class_for_path(const char *vpath) {
    const struct vfs_route_entry *route = vfs_route_for_path(vpath);

    if (!route) {
        return VFS_BACKING_PERSISTENT;
    }

    return route->backing_class;
}

const char *vfs_backing_root_for_class(enum vfs_backing_class cls) {
    switch (cls) {
        case VFS_BACKING_PERSISTENT:
            return vfs_persistent_root;
        case VFS_BACKING_CACHE:
            return vfs_cache_root;
        case VFS_BACKING_TEMP:
            return vfs_temp_root;
        case VFS_BACKING_SYNTHETIC:
            /* Synthetic filesystems don't have backing paths */
            return NULL;
        case VFS_BACKING_EXTERNAL:
            /* External paths handled separately */
            return NULL;
        default:
            return vfs_persistent_root;
    }
}

const char *vfs_persistent_backing_root(void) {
    if (vfs_ensure_backing_initialized() < 0) {
        return NULL;
    }
    return vfs_persistent_root;
}

const char *vfs_cache_backing_root(void) {
    if (vfs_ensure_backing_initialized() < 0) {
        return NULL;
    }
    return vfs_cache_root;
}

const char *vfs_temp_backing_root(void) {
    if (vfs_ensure_backing_initialized() < 0) {
        return NULL;
    }
    return vfs_temp_root;
}

static int vfs_join_backing_root_for_route(const struct vfs_route_entry *route,
                                           const char *normalized_virtual_path, char *backing_path,
                                           size_t backing_path_len) {
    const char *backing_root;
    const char *relative_suffix;
    size_t root_len;
    size_t suffixlen;
    size_t total_len;

    if (!route || !normalized_virtual_path || !backing_path || backing_path_len == 0) {
        return -EINVAL;
    }

    backing_root = vfs_backing_root_for_class(route->backing_class);
    if (!backing_root) {
        /* Synthetic/external: no backing, return virtual path as-is or error */
        return -EOPNOTSUPP;
    }

    relative_suffix = vfs_relative_suffixfor_route(route, normalized_virtual_path);
    if (!relative_suffix) {
        return -EINVAL;
    }

    root_len = strlen(backing_root);
    suffixlen = strlen(relative_suffix);

    if (suffixlen == 0 || strcmp(relative_suffix, "/") == 0) {
        return vfs_copy_string(backing_root, backing_path, backing_path_len);
    }

    total_len = root_len + suffixlen;
    if (total_len + 1 > backing_path_len) {
        return -ENAMETOOLONG;
    }

    memcpy(backing_path, backing_root, root_len);
    memcpy(backing_path + root_len, relative_suffix, suffixlen + 1);
    return 0;
}

static int vfs_join_backing_root(const char *normalized_virtual_path, char *backing_path,
                              size_t backing_path_len) {
    const struct vfs_route_entry *route;

    if (vfs_ensure_backing_initialized() < 0) {
        return -EOPNOTSUPP;
    }

    route = vfs_route_for_path(normalized_virtual_path);
    if (!route) {
        return -ENOENT;
    }

    return vfs_join_backing_root_for_route(route, normalized_virtual_path, backing_path, backing_path_len);
}

const char *vfs_primary_backing_root(void) {
    if (vfs_ensure_backing_initialized() < 0) {
        return NULL;
    }
    return vfs_persistent_root;
}

const char *vfs_virtual_root(void) {
    return vfs_virtual_root_path;
}

struct fs_context *alloc_fs_struct(void) {
    struct fs_context *fs = __kmalloc_noprof(sizeof(struct fs_context), GFP_KERNEL | __GFP_ZERO);
    if (!fs)
        return NULL;

    atomic_set(&fs->users, 1);
    fs_mutex_init(&fs->lock);
    fs->umask = 022;
    fs->mnt_ns = vfs_alloc_mount_namespace();
    if (!fs->mnt_ns) {
        fs_mutex_destroy(&fs->lock);
        kfree(fs);
        return NULL;
    }

    return fs;
}

struct fs_context *get_fs_struct(struct fs_context *fs) {
    if (!fs) {
        return NULL;
    }
    atomic_inc(&fs->users);
    return fs;
}

void free_fs_struct(struct fs_context *fs) {
    if (!fs)
        return;
    if (atomic_dec_return(&fs->users) > 0)
        return;

    vfs_put_mount_namespace(fs->mnt_ns);
    fs_mutex_destroy(&fs->lock);
    kfree(fs);
}

struct fs_context *dup_fs_struct(struct fs_context *old) {
    if (!old)
        return NULL;

    struct fs_context *new = alloc_fs_struct();
    if (!new)
        return NULL;

    fs_mutex_lock(&old->lock);
    if (old->root)
        new->root = old->root;
    if (old->pwd)
        new->pwd = old->pwd;
    new->umask = old->umask;
    memcpy(new->root_path, old->root_path, MAX_PATH);
    memcpy(new->pwd_path, old->pwd_path, MAX_PATH);
    vfs_put_mount_namespace(new->mnt_ns);
    new->mnt_ns = vfs_get_mount_namespace(old->mnt_ns);
    if (!new->mnt_ns) {
        fs_mutex_unlock(&old->lock);
        free_fs_struct(new);
        return NULL;
    }
    fs_mutex_unlock(&old->lock);

    return new;
}

int fs_unshare_mount_namespace(struct fs_context *fs) {
    struct vfs_mount_namespace *new_ns;
    struct vfs_mount_namespace *old_ns;

    if (!fs) {
        return -EINVAL;
    }

    fs_mutex_lock(&fs->lock);
    old_ns = fs->mnt_ns;
    new_ns = vfs_dup_mount_namespace(old_ns);
    if (!new_ns) {
        fs_mutex_unlock(&fs->lock);
        return -ENOMEM;
    }
    fs->mnt_ns = new_ns;
    fs_mutex_unlock(&fs->lock);

    vfs_put_mount_namespace(old_ns);
    return 0;
}

uint64_t fs_mount_namespace_id(struct fs_context *fs) {
    uint64_t id;

    if (!fs || !fs->mnt_ns) {
        return 0;
    }

    fs_mutex_lock(&fs->lock);
    id = fs->mnt_ns ? fs->mnt_ns->ns_id : 0;
    fs_mutex_unlock(&fs->lock);
    return id;
}

unsigned int fs_mount_namespace_refs(struct fs_context *fs) {
    unsigned int refs;

    if (!fs || !fs->mnt_ns) {
        return 0;
    }

    fs_mutex_lock(&fs->lock);
    refs = fs->mnt_ns ? (unsigned int)atomic_read(&fs->mnt_ns->refs) : 0;
    fs_mutex_unlock(&fs->lock);
    return refs;
}

unsigned int fs_mount_namespace_active_mounts(struct fs_context *fs) {
    struct vfs_mount_namespace *mnt_ns;
    unsigned int count = 0;

    if (!fs || !fs->mnt_ns) {
        return 0;
    }

    fs_mutex_lock(&fs->lock);
    mnt_ns = fs->mnt_ns;
    if (mnt_ns) {
        fs_mutex_lock(&mnt_ns->lock);
        for (size_t i = 0; i < MAX_MOUNTS; i++) {
            if (mnt_ns->entries[i].active) {
                count++;
            }
        }
        fs_mutex_unlock(&mnt_ns->lock);
    }
    fs_mutex_unlock(&fs->lock);
    return count;
}

/* Initialize fs_struct with virtual root path */
int fs_init_root(struct fs_context *fs, const char *root_path) {
    if (!fs || !root_path)
        return -EINVAL;

    char normalized[MAX_PATH];
    if (vfs_normalize_linux_path(root_path, normalized, sizeof(normalized)) < 0)
        return -EINVAL;

    fs_mutex_lock(&fs->lock);
    memcpy(fs->root_path, normalized, MAX_PATH);
    /* Also set pwd to root if not already set */
    if (fs->pwd_path[0] == '\0')
        memcpy(fs->pwd_path, normalized, MAX_PATH);
    fs_mutex_unlock(&fs->lock);

    return 0;
}

/* Initialize fs_struct with virtual pwd path */
int fs_init_pwd(struct fs_context *fs, const char *pwd_path) {
    if (!fs || !pwd_path)
        return -EINVAL;

    char normalized[MAX_PATH];
    if (vfs_normalize_linux_path(pwd_path, normalized, sizeof(normalized)) < 0)
        return -EINVAL;

    fs_mutex_lock(&fs->lock);
    memcpy(fs->pwd_path, normalized, MAX_PATH);
    /* Also set root to pwd if not already set */
    if (fs->root_path[0] == '\0')
        memcpy(fs->root_path, normalized, MAX_PATH);
    fs_mutex_unlock(&fs->lock);

    return 0;
}

/* Set new pwd - task-aware path change */
int fs_set_pwd(struct fs_context *fs, const char *new_pwd) {
    return fs_init_pwd(fs, new_pwd);
}

/* Set new root - task-aware root change */
int fs_set_root(struct fs_context *fs, const char *new_root) {
    return fs_init_root(fs, new_root);
}

/* Bootstrap Linux identity/config baseline in private rootfs */
static int vfs_bootstrap_etc_files_impl(void) {
    const char *passwd_content =
        "root:x:0:0:root:/root:/bin/sh\n"
        "orlix:x:1000:1000:Orlix User:/home/orlix:/bin/sh\n";
    const char *group_content =
        "root:x:0:\n"
        "orlix:x:1000:\n";
    const char *hosts_content =
        "127.0.0.1\tlocalhost\n"
        "::1\t\tlocalhost ip6-localhost ip6-loopback\n";
    const char *resolv_content =
        "nameserver 8.8.8.8\n"
        "nameserver 8.8.4.4\n";

    char etc_path[MAX_PATH];
    char file_path[MAX_PATH];
    int fd;
    ssize_t written;
    size_t len;

    snprintf(etc_path, sizeof(etc_path), "%s/etc", vfs_persistent_backing_root());
    backing_ensure_directory(etc_path, 0755);

    /* Create /etc/passwd */
    snprintf(file_path, sizeof(file_path), "%s/passwd", etc_path);
    fd = backing_open(file_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) {
        len = strlen(passwd_content);
        written = backing_write(fd, passwd_content, len);
        backing_close(fd);
        if (written != (ssize_t)len) {
            /* Non-fatal: file creation may fail in constrained environments */
        }
    }

    /* Create /etc/group */
    snprintf(file_path, sizeof(file_path), "%s/group", etc_path);
    fd = backing_open(file_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) {
        len = strlen(group_content);
        written = backing_write(fd, group_content, len);
        backing_close(fd);
        if (written != (ssize_t)len) {
            /* Non-fatal: file creation may fail in constrained environments */
        }
    }

    /* Create /etc/hosts */
    snprintf(file_path, sizeof(file_path), "%s/hosts", etc_path);
    fd = backing_open(file_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) {
        len = strlen(hosts_content);
        written = backing_write(fd, hosts_content, len);
        backing_close(fd);
        if (written != (ssize_t)len) {
            /* Non-fatal: file creation may fail in constrained environments */
        }
    }

    /* Create /etc/resolv.conf */
    snprintf(file_path, sizeof(file_path), "%s/resolv.conf", etc_path);
    fd = backing_open(file_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) {
        len = strlen(resolv_content);
        written = backing_write(fd, resolv_content, len);
        backing_close(fd);
        if (written != (ssize_t)len) {
            /* Non-fatal: file creation may fail in constrained environments */
        }
    }

    return 0;
}

/* VFS operations - to be implemented in full */
int vfs_init(void) {
    return vfs_ensure_backing_initialized();
}

void vfs_deinit(void) {
    /* Reset VFS initialization state for cold boot/reboot */
    vfs_backing_initialized = 0;
    vfs_etc_bootstrapped = 0;
    atomic_set(&vfs_next_mount_id, 2);
    atomic_set(&vfs_next_mount_peer_group_id, 1);
    fs_mutex_lock(&vfs_detached_mount_lock);
    memset(vfs_detached_mount_refs, 0, sizeof(vfs_detached_mount_refs));
    fs_mutex_unlock(&vfs_detached_mount_lock);
    fs_mutex_lock(&vfs_metadata_lock);
    memset(vfs_metadata_table, 0, sizeof(vfs_metadata_table));
    fs_mutex_unlock(&vfs_metadata_lock);
}

int vfs_mount(const char *source, const char *target, const char *fstype, unsigned long flags,
              const void *data) {
    (void)data;
    char resolved_source[MAX_PATH];
    char resolved_target[MAX_PATH];
    char backing_source[MAX_PATH];
    char backing_target[MAX_PATH];
    struct stat source_stat;
    struct stat target_stat;
    int ret;
    int slot = -1;
    unsigned long supported_flags = MS_BIND | MS_REMOUNT | vfs_mount_attribute_flags() | MS_REC | MS_MOVE |
                                    vfs_mount_propagation_flags();
    unsigned long propagation = vfs_mount_selected_propagation(flags);
    struct vfs_mount_namespace *mnt_ns = vfs_task_mount_namespace();
    bool remount = (flags & MS_REMOUNT) != 0;
    bool move_mount = (flags & MS_MOVE) != 0;
    bool cgroup2_mount = fstype && strcmp(fstype, "cgroup2") == 0;

    if ((!source && !remount) || !target) {
        return -EFAULT;
    }
    if ((!remount && source[0] == '\0') || target[0] == '\0') {
        return -ENOENT;
    }
    if ((flags & ~supported_flags) != 0) {
        return -EINVAL;
    }
    if (propagation == (unsigned long)-1) {
        return -EINVAL;
    }
    if (move_mount && (flags & ~MS_MOVE) != 0) {
        return -EINVAL;
    }
    if (!remount && !move_mount && (flags & MS_BIND) == 0 && !cgroup2_mount) {
        return -ENOSYS;
    }
    if (remount && (flags & MS_BIND) == 0) {
        return -EINVAL;
    }
    if (!mnt_ns) {
        return -ESRCH;
    }
    if (!vfs_current_has_mount_admin(mnt_ns)) {
        return -EPERM;
    }
    if (fstype && fstype[0] != '\0' && strcmp(fstype, "bind") != 0 && !cgroup2_mount) {
        return -EINVAL;
    }

    ret = vfs_resolve_virtual_path_at(AT_FDCWD, target, resolved_target, sizeof(resolved_target));
    if (ret != 0) {
        return ret;
    }
    if (strcmp(resolved_target, "/") == 0) {
        return -EINVAL;
    }

    if (remount) {
        fs_mutex_lock(&mnt_ns->lock);
        for (size_t i = 0; i < MAX_MOUNTS; i++) {
            if (mnt_ns->entries[i].active && strcmp(mnt_ns->entries[i].target, resolved_target) == 0) {
                if ((flags & MS_REC) != 0) {
                    for (size_t child = 0; child < MAX_MOUNTS; child++) {
                        if (mnt_ns->entries[child].active &&
                            vfs_mount_target_matches_tree(mnt_ns->entries[child].target, resolved_target)) {
                            vfs_mount_apply_remount_attributes(&mnt_ns->entries[child], flags);
                        }
                    }
                } else {
                    vfs_mount_apply_remount_attributes(&mnt_ns->entries[i], flags);
                }
                if (propagation != 0) {
                    if ((flags & MS_REC) != 0) {
                        vfs_mount_set_tree_propagation_locked(mnt_ns, resolved_target, propagation);
                    } else {
                        vfs_mount_set_propagation_locked(mnt_ns, &mnt_ns->entries[i], propagation);
                    }
                }
                fs_mutex_unlock(&mnt_ns->lock);
                return 0;
            }
            if (!mnt_ns->entries[i].active && slot < 0) {
                slot = (int)i;
            }
        }
        if ((strcmp(resolved_target, "/dev") == 0 ||
             strcmp(resolved_target, "/proc") == 0 ||
             strcmp(resolved_target, "/sys") == 0) && slot >= 0) {
            ret = vfs_mount_copy_entry(&mnt_ns->entries[slot], resolved_target, resolved_target,
                                       "bind", (flags & vfs_mount_attribute_flags()) | MS_BIND,
                                       propagation);
            if (ret == 0) {
                vfs_mount_assign_propagation_ids_locked(mnt_ns, &mnt_ns->entries[slot]);
            }
            fs_mutex_unlock(&mnt_ns->lock);
            return ret;
        }
        fs_mutex_unlock(&mnt_ns->lock);
        return -EINVAL;
    }

    if (cgroup2_mount) {
        memcpy(resolved_source, "/sys/fs/cgroup", sizeof("/sys/fs/cgroup"));
    } else {
        ret = vfs_resolve_virtual_path_at(AT_FDCWD, source, resolved_source, sizeof(resolved_source));
        if (ret != 0) {
            return ret;
        }
    }

    if (move_mount) {
        ret = vfs_translate_path(resolved_target, backing_target, sizeof(backing_target));
        if (ret != 0) {
            return ret;
        }
        ret = backing_stat(backing_target, &target_stat);
        if (ret != 0) {
            return ret;
        }
        fs_mutex_lock(&mnt_ns->lock);
        ret = vfs_mount_move_tree_with_propagation_locked(mnt_ns, resolved_source, resolved_target);
        fs_mutex_unlock(&mnt_ns->lock);
        return ret;
    }

    ret = vfs_translate_path(resolved_target, backing_target, sizeof(backing_target));
    if (ret != 0) {
        return ret;
    }

    ret = backing_stat(backing_target, &target_stat);
    if (ret != 0) {
        return ret;
    }
    if (!cgroup2_mount) {
        ret = vfs_translate_path(resolved_source, backing_source, sizeof(backing_source));
        if (ret != 0) {
            return ret;
        }
        ret = backing_stat(backing_source, &source_stat);
        if (ret != 0) {
            return ret;
        }
        if ((S_ISDIR(source_stat.st_mode) && !S_ISDIR(target_stat.st_mode)) ||
            (!S_ISDIR(source_stat.st_mode) && S_ISDIR(target_stat.st_mode))) {
            return -ENOTDIR;
        }
    } else if (!S_ISDIR(target_stat.st_mode)) {
        return -ENOTDIR;
    }

    fs_mutex_lock(&mnt_ns->lock);
    for (size_t i = 0; i < MAX_MOUNTS; i++) {
        if (mnt_ns->entries[i].active && strcmp(mnt_ns->entries[i].target, resolved_target) == 0) {
            fs_mutex_unlock(&mnt_ns->lock);
            return -EBUSY;
        }
        if (!mnt_ns->entries[i].active && slot < 0) {
            slot = (int)i;
        }
    }
    if (slot < 0) {
        fs_mutex_unlock(&mnt_ns->lock);
        return -ENOSPC;
    }

    ret = vfs_mount_copy_entry(&mnt_ns->entries[slot], resolved_source, resolved_target,
                               fstype, flags, propagation);
    if (ret != 0) {
        fs_mutex_unlock(&mnt_ns->lock);
        return ret;
    }
    vfs_mount_assign_propagation_ids_locked(mnt_ns, &mnt_ns->entries[slot]);
    if ((flags & MS_REC) != 0) {
        ret = vfs_mount_clone_recursive_children_locked(mnt_ns, slot);
        if (ret != 0) {
            memset(&mnt_ns->entries[slot], 0, sizeof(mnt_ns->entries[slot]));
            fs_mutex_unlock(&mnt_ns->lock);
            return ret;
        }
    }
    ret = vfs_mount_propagate_shared_child_locked(mnt_ns, slot);
    if (ret != 0) {
        memset(&mnt_ns->entries[slot], 0, sizeof(mnt_ns->entries[slot]));
        fs_mutex_unlock(&mnt_ns->lock);
        return ret;
    }
    fs_mutex_unlock(&mnt_ns->lock);

    return 0;
}

enum vfs_umount_operation {
    VFS_UMOUNT_NORMAL,
    VFS_UMOUNT_EXPIRE,
    VFS_UMOUNT_DETACH,
    VFS_UMOUNT_FORCE,
};

static int vfs_umount_with_operation(const char *target, enum vfs_umount_operation operation) {
    char resolved_target[MAX_PATH];
    int ret;
    struct vfs_mount_namespace *mnt_ns = vfs_task_mount_namespace();

    if (!target) {
        return -EFAULT;
    }
    if (target[0] == '\0') {
        return -ENOENT;
    }
    if (!mnt_ns) {
        return -ESRCH;
    }
    if (!vfs_current_has_mount_admin(mnt_ns)) {
        return -EPERM;
    }

    if (strcmp(target, ".") == 0) {
        struct task *task = task_current();

        if (!task || !task->fs) {
            return -ESRCH;
        }
        fs_mutex_lock(&task->fs->lock);
        ret = vfs_copy_string(task->fs->pwd_path, resolved_target, sizeof(resolved_target));
        fs_mutex_unlock(&task->fs->lock);
        if (ret != 0) {
            return ret;
        }
    } else {
        ret = vfs_resolve_virtual_path_at(AT_FDCWD, target, resolved_target, sizeof(resolved_target));
        if (ret != 0) {
            return ret;
        }
    }

    fs_mutex_lock(&mnt_ns->lock);
    for (size_t i = 0; i < MAX_MOUNTS; i++) {
        if (mnt_ns->entries[i].active && strcmp(mnt_ns->entries[i].target, resolved_target) == 0) {
            bool pinned = fdtable_has_open_path_under_mount_namespace_impl(mnt_ns->ns_id,
                                                                           resolved_target) ||
                          vfs_any_task_fs_pins_mount_tree(mnt_ns->ns_id, resolved_target);

            if (operation == VFS_UMOUNT_NORMAL && pinned) {
                fs_mutex_unlock(&mnt_ns->lock);
                return -EBUSY;
            }

            if (operation == VFS_UMOUNT_EXPIRE && pinned) {
                fs_mutex_unlock(&mnt_ns->lock);
                return -EBUSY;
            }
            if (operation == VFS_UMOUNT_EXPIRE && !mnt_ns->entries[i].expiry_candidate) {
                mnt_ns->entries[i].expiry_candidate = true;
                fs_mutex_unlock(&mnt_ns->lock);
                return -EAGAIN;
            }

            if ((operation == VFS_UMOUNT_DETACH || operation == VFS_UMOUNT_FORCE) && pinned) {
                ret = vfs_detached_mount_record(mnt_ns->ns_id, resolved_target);
                if (ret != 0) {
                    fs_mutex_unlock(&mnt_ns->lock);
                    return ret;
                }
            }
            vfs_umount_propagate_tree_locked(mnt_ns, resolved_target);
            vfs_umount_remove_tree_locked(mnt_ns, resolved_target);
            fs_mutex_unlock(&mnt_ns->lock);
            return 0;
        }
    }
    fs_mutex_unlock(&mnt_ns->lock);

    return -EINVAL;
}

int vfs_umount(const char *target) {
    return vfs_umount_with_operation(target, VFS_UMOUNT_NORMAL);
}

int vfs_umount_expire(const char *target) {
    return vfs_umount_with_operation(target, VFS_UMOUNT_EXPIRE);
}

int vfs_umount_lazy(const char *target) {
    return vfs_umount_with_operation(target, VFS_UMOUNT_DETACH);
}

int vfs_umount_force(const char *target) {
    return vfs_umount_with_operation(target, VFS_UMOUNT_FORCE);
}

int vfs_open(const char *path, int flags, uint32_t mode, int *target_fd) {
    int real_fd;

    if (!path || !target_fd) {
        return -EFAULT;
    }

    if (vfs_path_is_synthetic(path)) {
        return -EOPNOTSUPP;
    }

    /* Current in-repo callers pass the host platform's open flags.
     * Preserve the actual call surface used by OrlixKernel while the Linux-facing
     * contract is modeled in higher layers. Validate only combinations we can
     * represent coherently now.
     */
    if ((flags & O_EXCL) && !(flags & O_CREAT)) {
        return -EINVAL;
    }

    real_fd = backing_open(path, flags, mode);
    if (real_fd < 0) {
        return real_fd;
    }

    *target_fd = real_fd;
    return 0;
}

static int vfs_join_virtual_path(const char *base_path, const char *suffix, char *joined_path,
                                 size_t joined_path_len) {
    size_t base_len;
    size_t suffixlen;
    size_t suffixoffset;

    if (!base_path || !suffix || !joined_path || joined_path_len == 0) {
        return -EINVAL;
    }

    base_len = strlen(base_path);
    suffixlen = strlen(suffix);
    suffixoffset = (suffix[0] == '/') ? 1 : 0;

    if (base_len == 0) {
        return -EINVAL;
    }

    if (strcmp(base_path, "/") == 0) {
        if (suffixlen - suffixoffset + 1 >= joined_path_len) {
            return -ENAMETOOLONG;
        }
        joined_path[0] = '/';
        memcpy(joined_path + 1, suffix + suffixoffset, suffixlen - suffixoffset + 1);
        return 0;
    }

    if (base_len + 1 + suffixlen - suffixoffset >= joined_path_len) {
        return -ENAMETOOLONG;
    }

    memcpy(joined_path, base_path, base_len);
    joined_path[base_len] = '/';
    memcpy(joined_path + base_len + 1, suffix + suffixoffset, suffixlen - suffixoffset + 1);
    return 0;
}

static int vfs_lstat_resolved_virtual_path(const char *resolved_vpath, struct stat *st) {
    char mounted_path[MAX_PATH];
    char translated_path[MAX_PATH];
    int ret;

    if (!resolved_vpath || !st) {
        return -EINVAL;
    }

    if (vfs_path_is_synthetic(resolved_vpath)) {
        return -ENOENT;
    }

    ret = vfs_apply_mounts(resolved_vpath, mounted_path, sizeof(mounted_path));
    if (ret != 0) {
        return ret;
    }
    ret = vfs_join_backing_root(mounted_path, translated_path, sizeof(translated_path));
    if (ret != 0) {
        return ret;
    }

    return vfs_lstat(translated_path, st);
}

static int vfs_readlink_resolved_virtual_path(const char *resolved_vpath, char *target,
                                              size_t target_len) {
    char mounted_path[MAX_PATH];
    char translated_path[MAX_PATH];
    ssize_t len;
    int ret;

    if (!resolved_vpath || !target || target_len == 0) {
        return -EINVAL;
    }

    if (vfs_path_is_synthetic(resolved_vpath)) {
        return -ENOENT;
    }

    ret = vfs_apply_mounts(resolved_vpath, mounted_path, sizeof(mounted_path));
    if (ret != 0) {
        return ret;
    }
    ret = vfs_join_backing_root(mounted_path, translated_path, sizeof(translated_path));
    if (ret != 0) {
        return ret;
    }

    len = backing_readlink(translated_path, target, target_len - 1);
    if (len < 0) {
        return (int)len;
    }
    target[len] = '\0';
    return 0;
}

static bool vfs_path_is_route_prefix(const char *vpath) {
    size_t vpath_len;

    if (!vpath) {
        return false;
    }

    if (vfs_path_is_linux_route(vpath)) {
        return true;
    }

    vpath_len = strlen(vpath);
    for (size_t i = 0; i < vfs_route_table_count; i++) {
        const char *prefix = vfs_route_table[i].linux_prefix;
        if (strncmp(prefix, vpath, vpath_len) == 0 && prefix[vpath_len] == '/') {
            return true;
        }
    }

    return false;
}

static int vfs_replace_symlink_path(const char *current_path, size_t component_end,
                                    const char *target, const char *root_path,
                                    char *resolved_path, size_t resolved_path_len) {
    char parent[MAX_PATH];
    char replacement[MAX_PATH];
    char joined[MAX_PATH];
    const char *suffix = current_path[component_end] == '/' ? current_path + component_end + 1 : NULL;
    int ret;

    if (!current_path || !target || !root_path || !resolved_path || resolved_path_len == 0) {
        return -EINVAL;
    }

    if (target[0] == '/') {
        ret = vfs_join_virtual_path(root_path, target, replacement, sizeof(replacement));
    } else {
        size_t component_len = component_end;
        if (component_len >= sizeof(parent)) {
            return -ENAMETOOLONG;
        }
        memcpy(parent, current_path, component_len);
        parent[component_len] = '\0';
        ret = vfs_parent_path(parent, parent, sizeof(parent));
        if (ret != 0) {
            return ret;
        }
        ret = vfs_join_virtual_path(parent, target, replacement, sizeof(replacement));
    }
    if (ret != 0) {
        return ret;
    }

    if (suffix && suffix[0] != '\0') {
        ret = vfs_join_virtual_path(replacement, suffix, joined, sizeof(joined));
        if (ret != 0) {
            return ret;
        }
        return vfs_normalize_linux_path(joined, resolved_path, resolved_path_len);
    }

    return vfs_normalize_linux_path(replacement, resolved_path, resolved_path_len);
}

static int vfs_resolve_symlink_path(const char *normalized_path, const char *root_path,
                                    int follow_final_symlink, char *resolved_vpath,
                                    size_t resolved_vpath_len) {
    char work_path[MAX_PATH];
    unsigned int symlink_count = 0;
    int ret;

    ret = vfs_copy_string(normalized_path, work_path, sizeof(work_path));
    if (ret != 0) {
        return ret;
    }

    for (;;) {
        size_t offset = 1;
        bool restarted = false;

        if (strcmp(work_path, "/") == 0) {
            return vfs_copy_string(work_path, resolved_vpath, resolved_vpath_len);
        }

        while (work_path[offset] != '\0') {
            char component_path[MAX_PATH];
            char target[MAX_PATH];
            struct stat st;
            size_t component_end = offset;
            bool is_final;

            while (work_path[component_end] != '\0' && work_path[component_end] != '/') {
                component_end++;
            }
            is_final = work_path[component_end] == '\0';
            if (component_end >= sizeof(component_path)) {
                return -ENAMETOOLONG;
            }

            memcpy(component_path, work_path, component_end);
            component_path[component_end] = '\0';

            if (vfs_path_is_synthetic(component_path)) {
                return vfs_copy_string(work_path, resolved_vpath, resolved_vpath_len);
            }

            ret = vfs_lstat_resolved_virtual_path(component_path, &st);
            if (ret == -ENOENT) {
                if (!is_final && vfs_path_is_route_prefix(component_path)) {
                    offset = component_end + 1;
                    continue;
                }
                if (is_final) {
                    return vfs_copy_string(work_path, resolved_vpath, resolved_vpath_len);
                }
                return -ENOENT;
            }
            if (ret != 0) {
                return ret;
            }

            if (S_ISLNK(st.st_mode) && (!is_final || follow_final_symlink)) {
                if (++symlink_count > VFS_SYMLINK_MAX) {
                    return -ELOOP;
                }

                ret = vfs_readlink_resolved_virtual_path(component_path, target, sizeof(target));
                if (ret != 0) {
                    return ret;
                }
                ret = vfs_replace_symlink_path(work_path, component_end, target, root_path,
                                               work_path, sizeof(work_path));
                if (ret != 0) {
                    return ret;
                }
                restarted = true;
                break;
            }

            if (is_final) {
                return vfs_copy_string(work_path, resolved_vpath, resolved_vpath_len);
            }
            offset = component_end + 1;
        }

        if (!restarted) {
            return vfs_copy_string(work_path, resolved_vpath, resolved_vpath_len);
        }
    }
}

int vfs_resolve_virtual_path_task(const char *vpath, char *resolved_vpath, size_t resolved_vpath_len,
                                  struct fs_context *fs) {
    char work_buffer[MAX_PATH];
    const char *root_path;
    const char *pwd_path;
    int ret;

    if (!vpath || !resolved_vpath || resolved_vpath_len == 0) {
        return -EINVAL;
    }

    root_path = (fs && fs->root_path[0] != '\0') ? fs->root_path : vfs_virtual_root_path;
    pwd_path = (fs && fs->pwd_path[0] != '\0') ? fs->pwd_path : root_path;

    if (vpath[0] == '/') {
        ret = vfs_join_virtual_path(root_path, vpath, work_buffer, sizeof(work_buffer));
    } else {
        ret = vfs_join_virtual_path(pwd_path, vpath, work_buffer, sizeof(work_buffer));
    }
    if (ret < 0) {
        return ret;
    }

    return vfs_normalize_linux_path(work_buffer, resolved_vpath, resolved_vpath_len);
}

int vfs_resolve_virtual_path_task_follow(const char *vpath, char *resolved_vpath,
                                         size_t resolved_vpath_len, struct fs_context *fs,
                                         int follow_final_symlink) {
    char normalized_path[MAX_PATH];
    const char *root_path;
    int ret;

    if (!vpath || !resolved_vpath || resolved_vpath_len == 0) {
        return -EINVAL;
    }

    root_path = (fs && fs->root_path[0] != '\0') ? fs->root_path : vfs_virtual_root_path;
    ret = vfs_resolve_virtual_path_task(vpath, normalized_path, sizeof(normalized_path), fs);
    if (ret != 0) {
        return ret;
    }

    return vfs_resolve_symlink_path(normalized_path, root_path, follow_final_symlink,
                                    resolved_vpath, resolved_vpath_len);
}

int vfs_getcwd_path_task(struct fs_context *fs, char *vpath, size_t vpath_len) {
    const char *pwd_path;
    const char *root_path;
    size_t root_len;

    if (!vpath || vpath_len == 0) {
        return -EINVAL;
    }

    root_path = (fs && fs->root_path[0] != '\0') ? fs->root_path : vfs_virtual_root_path;
    pwd_path = (fs && fs->pwd_path[0] != '\0') ? fs->pwd_path : vfs_virtual_root_path;

    if (strcmp(root_path, "/") == 0) {
        return vfs_copy_string(pwd_path, vpath, vpath_len);
    }

    root_len = strlen(root_path);
    if (strcmp(pwd_path, root_path) == 0) {
        return vfs_copy_string("/", vpath, vpath_len);
    }

    if (strncmp(pwd_path, root_path, root_len) == 0 && pwd_path[root_len] == '/') {
        return vfs_copy_string(pwd_path + root_len, vpath, vpath_len);
    }

    return vfs_copy_string(pwd_path, vpath, vpath_len);
}

int vfs_resolve_virtual_path_at(int dirfd, const char *vpath, char *resolved_vpath,
                                size_t resolved_vpath_len) {
    struct task *task;
    struct fs_context *fs = NULL;
    char dir_virtual_path[MAX_PATH];
    char joined_virtual_path[MAX_PATH];
    void *entry;
    int ret;

    if (!vpath || !resolved_vpath || resolved_vpath_len == 0) {
        return -EINVAL;
    }

    if (vpath[0] == '/' || dirfd == AT_FDCWD) {
        task = task_current();
        if (task) {
            fs = task->fs;
        }
        return vfs_resolve_virtual_path_task(vpath, resolved_vpath, resolved_vpath_len, fs);
    }

    entry = get_fd_entry_impl(dirfd);
    if (!entry) {
        return -EBADF;
    }

    if (!get_fd_is_dir_impl(entry)) {
        put_fd_entry_impl(entry);
        return -ENOTDIR;
    }

    ret = get_fd_path_impl(entry, dir_virtual_path, sizeof(dir_virtual_path));
    put_fd_entry_impl(entry);
    if (ret != 0) {
        return ret;
    }

    ret = vfs_join_virtual_path(dir_virtual_path, vpath, joined_virtual_path, sizeof(joined_virtual_path));
    if (ret != 0) {
        return ret;
    }

    return vfs_normalize_linux_path(joined_virtual_path, resolved_vpath, resolved_vpath_len);
}

int vfs_resolve_virtual_path_at_follow(int dirfd, const char *vpath, char *resolved_vpath,
                                       size_t resolved_vpath_len, int follow_final_symlink) {
    struct task *task;
    struct fs_context *fs = NULL;
    char normalized_path[MAX_PATH];
    const char *root_path;
    int ret;

    if (!vpath || !resolved_vpath || resolved_vpath_len == 0) {
        return -EINVAL;
    }

    task = task_current();
    if (task) {
        fs = task->fs;
    }
    root_path = (fs && fs->root_path[0] != '\0') ? fs->root_path : vfs_virtual_root_path;

    ret = vfs_resolve_virtual_path_at(dirfd, vpath, normalized_path, sizeof(normalized_path));
    if (ret != 0) {
        return ret;
    }

    return vfs_resolve_symlink_path(normalized_path, root_path, follow_final_symlink,
                                    resolved_vpath, resolved_vpath_len);
}

int vfs_translate_path_task(const char *vpath, char *backing_path, size_t backing_path_len,
                            struct fs_context *fs) {
    char resolved_virtual[MAX_PATH];
    char mounted_virtual[MAX_PATH];
    int ret;

    if (!vpath || !backing_path || backing_path_len == 0) {
        return -EINVAL;
    }

    ret = vfs_resolve_virtual_path_task(vpath, resolved_virtual, sizeof(resolved_virtual), fs);
    if (ret < 0) {
        return ret;
    }

    ret = vfs_apply_mounts(resolved_virtual, mounted_virtual, sizeof(mounted_virtual));
    if (ret != 0) {
        return ret;
    }

    return vfs_join_backing_root(mounted_virtual, backing_path, backing_path_len);
}

int vfs_translate_path_at(int dirfd, const char *vpath, char *backing_path, size_t backing_path_len) {
    char resolved_virtual[MAX_PATH];
    char mounted_virtual[MAX_PATH];
    int ret;

    if (!vpath || !backing_path || backing_path_len == 0) {
        return -EINVAL;
    }

    ret = vfs_resolve_virtual_path_at(dirfd, vpath, resolved_virtual, sizeof(resolved_virtual));
    if (ret < 0) {
        return ret;
    }

    ret = vfs_apply_mounts(resolved_virtual, mounted_virtual, sizeof(mounted_virtual));
    if (ret != 0) {
        return ret;
    }

    return vfs_join_backing_root(mounted_virtual, backing_path, backing_path_len);
}

/* Legacy API: translate path using hardcoded root (for backward compatibility) */
int vfs_translate_path(const char *vpath, char *backing_path, size_t backing_path_len) {
    return vfs_translate_path_task(vpath, backing_path, backing_path_len, NULL);
}

/* Backing initialization - must be called before path translation */
static int vfs_ensure_backing_initialized(void) {
    if (vfs_backing_initialized) {
        return 0;
    }

    int ret;

    /* Discover persistent (Application Support) root */
    ret = backing_root_discover_persistent(vfs_persistent_root, sizeof(vfs_persistent_root));
    if (ret < 0) {
        return -EOPNOTSUPP;
    }

    /* Discover cache root */
    ret = backing_root_discover_cache(vfs_cache_root, sizeof(vfs_cache_root));
    if (ret < 0) {
        /* Fall back to persistent if caches not available */
        strncpy(vfs_cache_root, vfs_persistent_root, sizeof(vfs_cache_root) - 1);
        vfs_cache_root[sizeof(vfs_cache_root) - 1] = '\0';
    }

    /* Discover temp root */
    ret = backing_root_discover_temp(vfs_temp_root, sizeof(vfs_temp_root));
    if (ret < 0) {
        /* Fall back to temporary subdirectory of persistent */
        snprintf(vfs_temp_root, sizeof(vfs_temp_root), "%s/.orlix.tmp", vfs_persistent_root);
    }

    vfs_backing_initialized = 1;

    if (!vfs_etc_bootstrapped) {
        ret = vfs_bootstrap_etc_files_impl();
        if (ret != 0) {
            return ret;
        }
        vfs_etc_bootstrapped = 1;
    }

    return 0;
}

int vfs_reverse_translate(const char *backing_path, char *vpath, size_t vpath_len) {
    const struct vfs_route_entry *best_route = NULL;
    const char *best_backing_suffix = NULL;
    size_t best_prefklen = 0;
    size_t i;
    int ret;

    if (!backing_path || !vpath || vpath_len == 0) {
        return -EINVAL;
    }

    ret = vfs_ensure_backing_initialized();
    if (ret < 0) {
        return ret;
    }

    for (i = 0; i < vfs_route_table_count; i++) {
        const struct vfs_route_entry *route = &vfs_route_table[i];
        const char *backing_root;
        const char *backing_suffix;
        size_t root_len;
        size_t prefklen;
        char route_backing_prefix[MAX_PATH];

        if (!route->reverse_linux_prefix) {
            continue;
        }

        backing_root = vfs_backing_root_for_class(route->backing_class);
        if (!backing_root) {
            continue;
        }

        root_len = strlen(backing_root);
        if (route->strip_linux_prefix || strcmp(route->reverse_linux_prefix, "/") == 0) {
            if (vfs_copy_string(backing_root, route_backing_prefix, sizeof(route_backing_prefix)) != 0) {
                continue;
            }
        } else {
            ret = snprintf(route_backing_prefix, sizeof(route_backing_prefix), "%s%s", backing_root,
                           route->reverse_linux_prefix);
            if (ret < 0 || (size_t)ret >= sizeof(route_backing_prefix)) {
                continue;
            }
        }

        prefklen = strlen(route_backing_prefix);
        if (strncmp(backing_path, route_backing_prefix, prefklen) != 0) {
            continue;
        }

        backing_suffix = backing_path + prefklen;
        if (*backing_suffix != '\0' && *backing_suffix != '/') {
            continue;
        }

        if (prefklen > best_prefklen) {
            best_route = route;
            best_backing_suffix = backing_suffix;
            best_prefklen = prefklen;
        }
    }

    if (!best_route || !best_backing_suffix) {
        return -EXDEV;
    }

    if (strcmp(best_route->reverse_linux_prefix, "/") == 0) {
        if (*best_backing_suffix == '\0') {
            return vfs_copy_string(vfs_virtual_root_path, vpath, vpath_len);
        }
        return vfs_normalize_linux_path(best_backing_suffix, vpath, vpath_len);
    }

    if (*best_backing_suffix == '\0') {
        return vfs_copy_string(best_route->reverse_linux_prefix, vpath, vpath_len);
    }

    {
        char work_buf[MAX_PATH];
        ret = snprintf(work_buf, sizeof(work_buf), "%s%s", best_route->reverse_linux_prefix,
                       best_backing_suffix);
        if (ret < 0 || (size_t)ret >= sizeof(work_buf)) {
            return -ENAMETOOLONG;
        }
        return vfs_normalize_linux_path(work_buf, vpath, vpath_len);
    }
}

int vfs_stat_path(const char *pathname, struct stat *statbuf) {
    if (!pathname || !statbuf) {
        return -EFAULT;
    }
    if (vfs_path_is_synthetic(pathname)) {
        return -ENOENT;
    }
    return backing_stat(pathname, statbuf);
}

int vfs_lstat(const char *pathname, struct stat *statbuf) {
    if (!pathname || !statbuf) {
        return -EFAULT;
    }
    if (vfs_path_is_synthetic(pathname)) {
        return -ENOENT;
    }
    return backing_lstat(pathname, statbuf);
}

static const char *vfs_proc_current_task_suffix(const char *vpath, char *self_path, size_t self_path_len) {
    struct task *task;
    char prefix[32];
    int ret;
    size_t prefix_len;

    if (!vpath) {
        return NULL;
    }
    if (strncmp(vpath, "/proc/self", 10) == 0 &&
        (vpath[10] == '\0' || vpath[10] == '/')) {
        return vpath + 10;
    }

    if (strncmp(vpath, "/proc/", 6) == 0 && vpath[6] >= '0' && vpath[6] <= '9') {
        int32_t pid;
        const char *endptr;
        if (vfs_parse_decimal_i32(vpath + 6, 0, &pid, &endptr) == 0 &&
            (*endptr == '\0' || *endptr == '/')) {
            struct task *target = task_lookup(pid);
            if (target) {
                task_put(target);
                return endptr;
            }
        }
    }

    task = task_current();
    if (!task) {
        return NULL;
    }

    ret = snprintf(prefix, sizeof(prefix), "/proc/%d", task->pid);
    if (ret < 0 || (size_t)ret >= sizeof(prefix)) {
        return NULL;
    }
    prefix_len = (size_t)ret;
    if (strncmp(vpath, prefix, prefix_len) != 0 ||
        (vpath[prefix_len] != '\0' && vpath[prefix_len] != '/')) {
        return NULL;
    }

    ret = snprintf(self_path, self_path_len, "/proc/self%s", vpath + prefix_len);
    if (ret < 0 || (size_t)ret >= self_path_len) {
        return NULL;
    }
    return self_path + 10;
}

static int vfs_proc_task_tid_for_path(const char *vpath, int32_t *tid_out,
                                      const char **suffix_out) {
    struct task *group_task = NULL;
    struct task *thread = NULL;
    const char *cursor;
    int32_t group_pid = -1;
    int32_t tid;
    const char *endptr;

    if (!vpath || !tid_out || !suffix_out) {
        return -EINVAL;
    }
    *tid_out = -1;
    *suffix_out = NULL;

    if (strncmp(vpath, "/proc/self", 10) == 0 &&
        (vpath[10] == '\0' || vpath[10] == '/')) {
        struct task *current = task_current();
        if (!current) {
            return -ENOENT;
        }
        group_pid = current->pid;
        cursor = vpath + 10;
    } else if (strncmp(vpath, "/proc/", 6) == 0 && vpath[6] >= '0' && vpath[6] <= '9') {
        if (vfs_parse_decimal_i32(vpath + 6, 0, &group_pid, (const char **)&endptr) != 0 ||
            (*endptr != '\0' && *endptr != '/')) {
            return -ENOENT;
        }
        cursor = endptr;
    } else {
        return -ENOENT;
    }

    if (strncmp(cursor, "/task/", 6) != 0 || cursor[6] == '\0') {
        return -ENOENT;
    }
    if (vfs_parse_decimal_i32(cursor + 6, 0, &tid, (const char **)&endptr) != 0 ||
        (*endptr != '\0' && *endptr != '/')) {
        return -ENOENT;
    }

    group_task = task_lookup(group_pid);
    thread = task_lookup((int32_t)tid);
    if (!group_task || !thread || group_task->tgid != thread->tgid) {
        if (group_task) {
            task_put(group_task);
        }
        if (thread) {
            task_put(thread);
        }
        return -ENOENT;
    }
    *tid_out = thread->pid;
    *suffix_out = endptr;
    task_put(group_task);
    task_put(thread);
    return 0;
}

int vfs_proc_target_pid_for_path(const char *vpath) {
    struct task *task;
    int32_t tid;
    const char *suffix;

    if (!vpath) {
        return -1;
    }
    if (vfs_proc_task_tid_for_path(vpath, &tid, &suffix) == 0) {
        (void)suffix;
        return tid;
    }
    if (strncmp(vpath, "/proc/self", 10) == 0 &&
        (vpath[10] == '\0' || vpath[10] == '/')) {
        task = task_current();
        return task ? task->pid : -1;
    }
    if (strncmp(vpath, "/proc/", 6) == 0 && vpath[6] >= '0' && vpath[6] <= '9') {
        int32_t pid;
        const char *endptr;
        if (vfs_parse_decimal_i32(vpath + 6, 0, &pid, &endptr) == 0 &&
            (*endptr == '\0' || *endptr == '/')) {
            struct task *target = task_lookup(pid);
            if (target) {
                int target_pid = target->pid;
                task_put(target);
                return target_pid;
            }
        }
    }
    return -1;
}

int vfs_proc_fd_num_for_path(const char *vpath, const char *leaf) {
    char mapped[MAX_PATH];
    const char *suffix;
    const char *endptr;
    int32_t fd_num;
    size_t leaf_len;
    int32_t tid;

    if (!vpath || !leaf) {
        return -EINVAL;
    }

    if (vfs_proc_task_tid_for_path(vpath, &tid, &suffix) == 0) {
        (void)tid;
    } else {
        suffix = vfs_proc_current_task_suffix(vpath, mapped, sizeof(mapped));
    }
    if (!suffix) {
        return -ENOENT;
    }

    leaf_len = strlen(leaf);
    if (strncmp(suffix, leaf, leaf_len) != 0 || suffix[leaf_len] == '\0') {
        return -ENOENT;
    }

    if (vfs_parse_decimal_i32(suffix + leaf_len, 1, &fd_num, &endptr) != 0 ||
        *endptr != '\0' || fd_num >= NR_OPEN_DEFAULT) {
        return -ENOENT;
    }
    return fd_num;
}

proc_self_path_class_t vfs_classify_proc_self_path(const char *vpath) {
    char mapped[MAX_PATH];
    const char *suffix;
    int32_t tid;
    int task_tid_path = 0;

    if (!vpath) {
        return PROC_SELF_NONE;
    }

    if (strcmp(vpath, "/proc/filesystems") == 0) {
        return PROC_ROOT_FILESYSTEMS_FILE;
    }
    if (strcmp(vpath, "/proc/meminfo") == 0) {
        return PROC_ROOT_MEMINFO_FILE;
    }
    if (strcmp(vpath, "/proc/cpuinfo") == 0) {
        return PROC_ROOT_CPUINFO_FILE;
    }

    if (vfs_proc_task_tid_for_path(vpath, &tid, &suffix) == 0) {
        (void)tid;
        task_tid_path = 1;
    } else {
        suffix = vfs_proc_current_task_suffix(vpath, mapped, sizeof(mapped));
    }
    if (!suffix) {
        return PROC_SELF_NONE;
    }

    if (strcmp(suffix, "") == 0) {
        return task_tid_path ? PROC_SELF_THREAD_DIR : PROC_SELF_DIR;
    }
    if (strcmp(suffix, "/task") == 0) {
        return PROC_SELF_TASK_DIR;
    }
    if (strncmp(suffix, "/task/", 6) == 0 && suffix[6] != '\0') {
        int32_t tid_value;
        const char *endptr;
        if (vfs_parse_decimal_i32(suffix + 6, 0, &tid_value, &endptr) == 0 &&
            *endptr == '\0') {
            return PROC_SELF_THREAD_DIR;
        }
    }
    if (strcmp(suffix, "/fd") == 0) {
        return PROC_SELF_FD_DIR;
    }
    if (strcmp(suffix, "/fdinfo") == 0) {
        return PROC_SELF_FDINFO_DIR;
    }
    if (strncmp(suffix, "/fd/", 4) == 0 && suffix[4] != '\0') {
        return PROC_SELF_FD_LINK;
    }
    if (strncmp(suffix, "/fdinfo/", 8) == 0 && suffix[8] != '\0') {
        return PROC_SELF_FDINFO_FILE;
    }
    if (strcmp(suffix, "/cwd") == 0) {
        return PROC_SELF_CWD_LINK;
    }
    if (strcmp(suffix, "/exe") == 0) {
        return PROC_SELF_EXE_LINK;
    }
    if (strcmp(suffix, "/cmdline") == 0) {
        return PROC_SELF_CMDLINE_FILE;
    }
    if (strcmp(suffix, "/environ") == 0) {
        return PROC_SELF_ENVIRON_FILE;
    }
    if (strcmp(suffix, "/comm") == 0) {
        return PROC_SELF_COMM_FILE;
    }
    if (strcmp(suffix, "/stat") == 0) {
        return PROC_SELF_STAT_FILE;
    }
    if (strcmp(suffix, "/statm") == 0) {
        return PROC_SELF_STATM_FILE;
    }
    if (strcmp(suffix, "/maps") == 0) {
        return PROC_SELF_MAPS_FILE;
    }
    if (strcmp(suffix, "/smaps") == 0) {
        return PROC_SELF_SMAPS_FILE;
    }
    if (strcmp(suffix, "/status") == 0) {
        return PROC_SELF_STATUS_FILE;
    }
    if (strcmp(suffix, "/cgroup") == 0) {
        return PROC_SELF_CGROUP_FILE;
    }
    if (strcmp(suffix, "/uid_map") == 0) {
        return PROC_SELF_UID_MAP_FILE;
    }
    if (strcmp(suffix, "/gid_map") == 0) {
        return PROC_SELF_GID_MAP_FILE;
    }
    if (strcmp(suffix, "/setgroups") == 0) {
        return PROC_SELF_SETGROUPS_FILE;
    }
    if (strcmp(suffix, "/mountinfo") == 0) {
        return PROC_SELF_MOUNTINFO_FILE;
    }
    if (strcmp(suffix, "/mounts") == 0) {
        return PROC_SELF_MOUNTS_FILE;
    }
    if (strcmp(suffix, "/ns") == 0) {
        return PROC_SELF_NS_DIR;
    }
    if ((strcmp(suffix, "/ns/mnt") == 0) ||
        (strcmp(suffix, "/ns/uts") == 0) ||
        (strcmp(suffix, "/ns/pid") == 0) ||
        (strcmp(suffix, "/ns/cgroup") == 0)) {
        return PROC_SELF_NS_LINK;
    }
    return PROC_SELF_NONE;
}

static struct task *vfs_task_for_proc_pid(int32_t pid);
static void vfs_put_proc_task(struct task *task);

bool vfs_proc_fd_exists_for_path(const char *vpath, int fd_num) {
    struct task *task;
    bool exists;

    if (fd_num < 0 || fd_num >= NR_OPEN_DEFAULT) {
        return false;
    }

    task = vfs_task_for_proc_pid(vfs_proc_target_pid_for_path(vpath));
    if (!task) {
        return false;
    }
    exists = fdtable_task_is_used_impl(task, fd_num);
    if (!exists && task == task_current()) {
        exists = fdtable_is_used_impl(fd_num);
    }
    vfs_put_proc_task(task);
    return exists;
}

int vfs_proc_self_fd_link_target(const char *vpath, char *target, size_t target_len) {
    int fd_num;

    fd_num = vfs_proc_fd_num_for_path(vpath, "/fd/");
    if (fd_num < 0) {
        return fd_num;
    }
    return vfs_proc_task_fd_link_target(vfs_proc_target_pid_for_path(vpath), fd_num, target, target_len);
}

int vfs_proc_task_fd_link_target(int32_t pid, int fd_num, char *target, size_t target_len) {
    struct task *task;
    void *entry;
    int ret;
    bool is_current_task;

    if (!target || target_len == 0) {
        return -EINVAL;
    }

    if (fd_num < 0 || fd_num >= NR_OPEN_DEFAULT) {
        return -ENOENT;
    }

    task = vfs_task_for_proc_pid(pid);
    if (!task) {
        return -ESRCH;
    }

    is_current_task = task == task_current();
    ret = fdtable_task_fd_path_impl(task, fd_num, target, target_len);
    vfs_put_proc_task(task);
    if (ret == 0) {
        return 0;
    }
    if (!is_current_task) {
        return -ENOENT;
    }

    if (!fdtable_is_used_impl(fd_num)) {
        return -ENOENT;
    }

    entry = get_fd_entry_impl(fd_num);
    if (!entry) {
        return -ENOENT;
    }

    ret = get_fd_path_impl(entry, target, target_len);
    if (ret == 0 && get_fd_path_deleted_impl(entry)) {
        const char deleted_suffix[] = " (deleted)";
        size_t target_used = strlen(target);
        size_t suffix_len = sizeof(deleted_suffix) - 1;
        if (target_used + suffix_len >= target_len) {
            put_fd_entry_impl(entry);
            return -ENAMETOOLONG;
        }
        memcpy(target + target_used, deleted_suffix, suffix_len + 1);
    }
    put_fd_entry_impl(entry);

    if (ret != 0) {
        return -ENOENT;
    }

    return 0;
}

int vfs_proc_self_cwd_target(char *target, size_t target_len) {
    struct task *task;

    if (!target || target_len == 0) {
        return -EINVAL;
    }

    task = task_current();
    if (!task || !task->fs) {
        return -ESRCH;
    }

    return vfs_getcwd_path_task(task->fs, target, target_len);
}

int vfs_proc_task_cwd_target(int32_t pid, char *target, size_t target_len) {
    struct task *task;
    int ret;

    if (!target || target_len == 0) {
        return -EINVAL;
    }

    task = vfs_task_for_proc_pid(pid);
    if (!task || !task->fs) {
        if (task) {
            vfs_put_proc_task(task);
        }
        return -ESRCH;
    }

    ret = vfs_getcwd_path_task(task->fs, target, target_len);
    vfs_put_proc_task(task);
    return ret;
}

int vfs_proc_self_exe_target(char *target, size_t target_len) {
    struct task *task;
    size_t exe_len;

    if (!target || target_len == 0) {
        return -EINVAL;
    }

    task = task_current();
    if (!task) {
        return -ESRCH;
    }

    if (task->exe[0] == '\0') {
        return -ENOENT;
    }

    exe_len = strlen(task->exe);
    if (exe_len >= target_len) {
        return -ENAMETOOLONG;
    }

    memcpy(target, task->exe, exe_len + 1);
    return 0;
}

int vfs_proc_task_exe_target(int32_t pid, char *target, size_t target_len) {
    struct task *task;
    size_t exe_len;

    if (!target || target_len == 0) {
        return -EINVAL;
    }

    task = vfs_task_for_proc_pid(pid);
    if (!task) {
        return -ESRCH;
    }

    if (task->exe[0] == '\0') {
        vfs_put_proc_task(task);
        return -ENOENT;
    }

    exe_len = strlen(task->exe);
    if (exe_len >= target_len) {
        vfs_put_proc_task(task);
        return -ENAMETOOLONG;
    }

    memcpy(target, task->exe, exe_len + 1);
    vfs_put_proc_task(task);
    return 0;
}

static const char *vfs_proc_self_ns_name(const char *vpath) {
    const char *suffix;
    char mapped[MAX_PATH];

    suffix = vfs_proc_current_task_suffix(vpath, mapped, sizeof(mapped));
    if (!suffix || strncmp(suffix, "/ns/", 4) != 0) {
        return NULL;
    }
    return suffix + 4;
}

int vfs_proc_self_ns_link_target(const char *vpath, char *target, size_t target_len) {
    struct task *task;
    const char *name;
    uint64_t id;
    int ret;

    if (!vpath || !target || target_len == 0) {
        return -EINVAL;
    }

    task = task_current();
    if (!task) {
        return -ESRCH;
    }

    name = vfs_proc_self_ns_name(vpath);
    if (!name) {
        return -EINVAL;
    }

    if (strcmp(name, "mnt") == 0) {
        id = fs_mount_namespace_id(task->fs);
    } else if (strcmp(name, "uts") == 0) {
        id = uts_namespace_id(task->uts_ns);
    } else if (strcmp(name, "pid") == 0) {
        id = (uint64_t)(task->pid_ns_level + 1);
    } else if (strcmp(name, "cgroup") == 0) {
        id = task_cgroup_namespace_id(task);
    } else {
        return -ENOENT;
    }

    if (id == 0) {
        return -ESRCH;
    }

    ret = snprintf(target, target_len, "%s:[%llu]", name, (unsigned long long)id);
    if (ret < 0) {
        return -EINVAL;
    }
    if ((size_t)ret >= target_len) {
        return -ENAMETOOLONG;
    }
    return 0;
}

static int vfs_proc_cmdline_content_for_task(struct task *task, char *buf, size_t buf_len) {
    size_t pos = 0;
    int i;

    if (!buf || buf_len == 0) {
        return -EINVAL;
    }
    if (!task) {
        return -ESRCH;
    }

    if (task->argc > 0 && task->argv[0] != NULL) {
        for (i = 0; i < task->argc && pos < buf_len; i++) {
            if (task->argv[i] == NULL) {
                break;
            }
            size_t arg_len = strlen(task->argv[i]);
            size_t copy_len = arg_len;
            if (pos + copy_len >= buf_len) {
                copy_len = buf_len - pos - 1;
            }
            if (copy_len > 0) {
                memcpy(buf + pos, task->argv[i], copy_len);
                pos += copy_len;
            }
            if (pos < buf_len) {
                buf[pos++] = '\0';
            }
        }
    } else if (task->exe[0] != '\0') {
        size_t exe_len = strlen(task->exe);
        size_t copy_len = exe_len;
        if (pos + copy_len >= buf_len) {
            copy_len = buf_len - pos - 1;
        }
        if (copy_len > 0) {
            memcpy(buf + pos, task->exe, copy_len);
            pos += copy_len;
        }
        if (pos < buf_len) {
            buf[pos++] = '\0';
        }
    } else {
        const char *fallback = "xctest";
        size_t fallback_len = strlen(fallback);
        size_t copy_len = fallback_len;
        if (pos + copy_len >= buf_len) {
            copy_len = buf_len - pos - 1;
        }
        if (copy_len > 0) {
            memcpy(buf + pos, fallback, copy_len);
            pos += copy_len;
        }
        if (pos < buf_len) {
            buf[pos++] = '\0';
        }
    }

    return (int)pos;
}

int vfs_proc_task_cmdline_content(int32_t pid, char *buf, size_t buf_len) {
    struct task *task = vfs_task_for_proc_pid(pid);
    int ret;

    if (!task) {
        return -ESRCH;
    }
    ret = vfs_proc_cmdline_content_for_task(task, buf, buf_len);
    vfs_put_proc_task(task);
    return ret;
}

int vfs_proc_self_cmdline_content(char *buf, size_t buf_len) {
    struct task *task = task_current();
    return vfs_proc_task_cmdline_content(task ? task->pid : -1, buf, buf_len);
}

static int vfs_proc_environ_content_for_task(struct task *task, char *buf, size_t buf_len) {
    size_t pos = 0;

    if (!buf || buf_len == 0) {
        return -EINVAL;
    }
    if (!task) {
        return -ESRCH;
    }

    for (int i = 0; i < task->envc && pos < buf_len; i++) {
        size_t env_len;
        size_t copy_len;

        if (!task->envp[i]) {
            break;
        }

        env_len = strlen(task->envp[i]);
        copy_len = env_len;
        if (pos + copy_len >= buf_len) {
            copy_len = buf_len - pos - 1;
        }
        if (copy_len > 0) {
            memcpy(buf + pos, task->envp[i], copy_len);
            pos += copy_len;
        }
        if (pos < buf_len) {
            buf[pos++] = '\0';
        }
    }

    return (int)pos;
}

int vfs_proc_task_environ_content(int32_t pid, char *buf, size_t buf_len) {
    struct task *task = vfs_task_for_proc_pid(pid);
    int ret;

    if (!task) {
        return -ESRCH;
    }
    ret = vfs_proc_environ_content_for_task(task, buf, buf_len);
    vfs_put_proc_task(task);
    return ret;
}

int vfs_proc_self_environ_content(char *buf, size_t buf_len) {
    struct task *task = task_current();
    return vfs_proc_task_environ_content(task ? task->pid : -1, buf, buf_len);
}

static int vfs_proc_comm_content_for_task(struct task *task, char *buf, size_t buf_len) {
    size_t comm_len;

    if (!buf || buf_len == 0) {
        return -EINVAL;
    }
    if (!task) {
        return -ESRCH;
    }

    comm_len = strnlen(task->comm, TASK_COMM_CAPACITY);
    if (comm_len + 1 >= buf_len) {
        if (buf_len > 1) {
            memcpy(buf, task->comm, buf_len - 2);
            buf[buf_len - 2] = '\n';
            buf[buf_len - 1] = '\0';
        }
        return (int)(buf_len - 1);
    }

    memcpy(buf, task->comm, comm_len);
    buf[comm_len] = '\n';
    buf[comm_len + 1] = '\0';
    return (int)(comm_len + 1);
}

int vfs_proc_task_comm_content(int32_t pid, char *buf, size_t buf_len) {
    struct task *task = vfs_task_for_proc_pid(pid);
    int ret;

    if (!task) {
        return -ESRCH;
    }
    ret = vfs_proc_comm_content_for_task(task, buf, buf_len);
    vfs_put_proc_task(task);
    return ret;
}

int vfs_proc_self_comm_content(char *buf, size_t buf_len) {
    struct task *task = task_current();
    return vfs_proc_task_comm_content(task ? task->pid : -1, buf, buf_len);
}

struct vfs_vm_accounting {
    uint64_t size_pages;
    uint64_t resident_pages;
    uint64_t resident_shared_pages;
    uint64_t text_pages;
    uint64_t data_pages;
    uint64_t stack_pages;
    uint64_t dirty_pages;
};

static void vfs_vm_account_task(const struct task *task, struct vfs_vm_accounting *acct) {
    memset(acct, 0, sizeof(*acct));
    if (!task || !task->mm) {
        return;
    }
    for (uint32_t i = 0; i < task->mm->vma_count; i++) {
        const struct task_vma *vma = &task->mm->vmas[i];
        uint64_t pages = vma->page_count;
        uint64_t resident_pages = vfs_vma_resident_page_count(vma);
        acct->size_pages += pages;
        acct->resident_pages += resident_pages;
        if (vma->shared) {
            acct->resident_shared_pages += resident_pages;
        }
        if (vma->kind == TASK_VMA_EXEC || vma->kind == TASK_VMA_INTERP) {
            acct->text_pages += pages;
        } else if (vma->kind == TASK_VMA_STACK) {
            acct->stack_pages += pages;
            acct->data_pages += pages;
        } else {
            acct->data_pages += pages;
        }
        if (vma->dirty_pages) {
            for (uint64_t page = 0; page < pages; page++) {
                if (vma->dirty_pages[page]) {
                    acct->dirty_pages++;
                }
            }
        }
    }
}

int vfs_proc_task_stat_content(int32_t pid, char *buf, size_t buf_len) {
    struct task *task;
    struct vfs_vm_accounting acct;
    int ret;
    char state_char;
    long long tty_nr = 0;
    long long tpgid = -1;
    unsigned long long starttime;
    unsigned long long vsize;
    unsigned long long rss;

    if (!buf || buf_len == 0) {
        return -EINVAL;
    }

    task = vfs_task_for_proc_pid(pid);
    if (!task) {
        return -ESRCH;
    }

    switch (atomic_read(&task->state)) {
        case RUN_STATE_RUNNING:
            state_char = 'R';
            break;
        case RUN_STATE_INTERRUPTIBLE:
            state_char = 'S';
            break;
        case RUN_STATE_UNINTERRUPTIBLE:
            state_char = 'D';
            break;
        case RUN_STATE_STOPPED:
            state_char = 'T';
            break;
        case RUN_STATE_ZOMBIE:
            state_char = 'Z';
            break;
        default:
            state_char = 'R';
            break;
    }

    task_mm_update_high_water_impl(task->mm);
    vfs_vm_account_task(task, &acct);
    if (task->tty) {
        tty_nr = task->tty->index;
        tpgid = task->tty->foreground_pgrp;
    }
    starttime = (unsigned long long)((task->start_time_ns + 9999999ULL) / 10000000ULL);
    vsize = (unsigned long long)(acct.size_pages * TASK_VMA_PAGE_SIZE);
    rss = (unsigned long long)acct.resident_pages;

    ret = snprintf(buf, buf_len,
        "%d (%s) %c %d %d %d %lld %lld 0 0 0 0 0 0 0 0 0 20 0 1 0 %llu %llu %llu 0 0 0 0 0 0 0 0 0 0 0 0 0 %d 0 0 0 0 0 0 0 0 0 0 0 0 0 0\n",
        task->pid,
        task->comm,
        state_char,
        task->ppid,
        task->pgid,
        task->sid,
        tty_nr,
        tpgid,
        starttime,
        vsize,
        rss,
        SIGCHLD
    );

    if (ret < 0) {
        vfs_put_proc_task(task);
        return -EINVAL;
    }
    if ((size_t)ret >= buf_len) {
        vfs_put_proc_task(task);
        return (int)(buf_len - 1);
    }
    vfs_put_proc_task(task);
    return ret;
}

int vfs_proc_self_stat_content(char *buf, size_t buf_len) {
    struct task *task = task_current();
    return vfs_proc_task_stat_content(task ? task->pid : -1, buf, buf_len);
}

static const char *vfs_proc_task_state_name(enum run_state state) {
    switch (state) {
    case RUN_STATE_RUNNING:
        return "running";
    case RUN_STATE_INTERRUPTIBLE:
        return "sleeping";
    case RUN_STATE_UNINTERRUPTIBLE:
        return "disk sleep";
    case RUN_STATE_STOPPED:
        return "stopped";
    case RUN_STATE_ZOMBIE:
        return "zombie";
    case RUN_STATE_DEAD:
        return "dead";
    default:
        return "running";
    }
}

static void vfs_proc_task_signal_status(const struct task *task,
                                        unsigned int *queued_out,
                                        uint64_t *private_pending_out,
                                        uint64_t *shared_pending_out,
                                        uint64_t *blocked_out,
                                        uint64_t *ignored_out,
                                        uint64_t *caught_out) {
    unsigned int queued = 0;
    uint64_t private_pending = 0;
    uint64_t shared_pending = 0;
    uint64_t blocked = 0;
    uint64_t ignored = 0;
    uint64_t caught = 0;

    if (task && task->signal) {
        kernel_mutex_lock(&task->signal->lock);
        private_pending = task->thread_pending_signals;
        shared_pending = task->signal->shared_pending.sig[0];
        blocked = task->signal->blocked.sig[0];
        queued = task->signal->queue.count < 0 ? 0U : (unsigned int)task->signal->queue.count;
        for (int sig = 1; sig <= KERNEL_SIG_NUM; sig++) {
            sighandler_t handler = task->signal->actions[sig - 1].handler;
            uint64_t bit = 1ULL << (sig - 1);

            if (!handler) {
                continue;
            }
            if ((uintptr_t)handler == 1U) {
                ignored |= bit;
            } else {
                caught |= bit;
            }
        }
        kernel_mutex_unlock(&task->signal->lock);
    }

    if (queued_out) {
        *queued_out = queued;
    }
    if (private_pending_out) {
        *private_pending_out = private_pending;
    }
    if (shared_pending_out) {
        *shared_pending_out = shared_pending;
    }
    if (blocked_out) {
        *blocked_out = blocked;
    }
    if (ignored_out) {
        *ignored_out = ignored;
    }
    if (caught_out) {
        *caught_out = caught;
    }
}

static struct task *vfs_task_for_proc_pid(int32_t pid) {
    struct task *current;

    current = task_current();
    if (pid <= 0 || (current && current->pid == pid)) {
        return current;
    }
    return task_lookup(pid);
}

static unsigned long long vfs_proc_fdinfo_mount_id_for_path(struct fs_context *fs, const char *path) {
    struct vfs_mount_namespace *mnt_ns;
    const struct vfs_mount_entry *entry;
    uint64_t id = 1;

    if (!path || path[0] != '/') {
        return 1;
    }

    mnt_ns = fs ? fs->mnt_ns : vfs_task_mount_namespace();
    if (!mnt_ns) {
        return 1;
    }

    fs_mutex_lock(&mnt_ns->lock);
    entry = vfs_find_mount_for_path_locked(path, mnt_ns);
    if (entry) {
        for (size_t i = 0; i < MAX_MOUNTS; i++) {
            if (&mnt_ns->entries[i] == entry) {
                id = vfs_mount_id_for_index_locked(mnt_ns, i);
                break;
            }
        }
    }
    fs_mutex_unlock(&mnt_ns->lock);
    return id == 0 ? 1 : id;
}

static void vfs_put_proc_task(struct task *task) {
    if (task && task != task_current()) {
        task_put(task);
    }
}

int vfs_proc_task_statm_content(int32_t pid, char *buf, size_t buf_len) {
    struct task *task;
    struct vfs_vm_accounting acct;
    int ret;

    if (!buf || buf_len == 0) {
        return -EINVAL;
    }

    task = vfs_task_for_proc_pid(pid);
    if (!task) {
        return -ESRCH;
    }
    vfs_vm_account_task(task, &acct);
    ret = snprintf(buf, buf_len, "%llu %llu %llu %llu 0 %llu 0\n",
                   (unsigned long long)acct.size_pages,
                   (unsigned long long)acct.resident_pages,
                   (unsigned long long)acct.resident_shared_pages,
                   (unsigned long long)acct.text_pages,
                   (unsigned long long)acct.data_pages);
    vfs_put_proc_task(task);

    if (ret < 0) {
        return -EINVAL;
    }
    if ((size_t)ret >= buf_len) {
        return (int)(buf_len - 1);
    }
    return ret;
}

int vfs_proc_self_statm_content(char *buf, size_t buf_len) {
    struct task *task = task_current();
    return vfs_proc_task_statm_content(task ? task->pid : -1, buf, buf_len);
}

static void vfs_maps_permissions(uint32_t flags, int shared, char perms[5]) {
    perms[0] = (flags & PF_R) ? 'r' : '-';
    perms[1] = (flags & PF_W) ? 'w' : '-';
    perms[2] = (flags & PF_X) ? 'x' : '-';
    perms[3] = shared ? 's' : 'p';
    perms[4] = '\0';
}

static const char *vfs_maps_kind_name(const struct task_vma *vma) {
    if (!vma) {
        return "";
    }
    switch (vma->kind) {
    case TASK_VMA_STACK:
        return "[stack]";
    case TASK_VMA_ANON:
        return "[anon]";
    default:
        return "";
    }
}

static int vfs_maps_path_for_vma(const struct task_vma *vma, char *path, size_t path_len) {
    void *entry;
    int ret;

    if (!vma || !path || path_len == 0) {
        return -EINVAL;
    }
    path[0] = '\0';
    if (vma->kind != TASK_VMA_FILE || vma->backing_fd < 0) {
        return 0;
    }
    if (vma->backing_path[0] != '\0') {
        return vfs_copy_string(vma->backing_path, path, path_len);
    }
    entry = get_fd_entry_impl(vma->backing_fd);
    if (!entry) {
        return 0;
    }
    ret = get_fd_path_impl(entry, path, path_len);
    put_fd_entry_impl(entry);
    return ret == 0 ? 0 : -ENOENT;
}

static int vfs_proc_task_maps_content_for_task(struct task *task, char *buf, size_t buf_len) {
    size_t pos = 0;

    if (!buf || buf_len == 0) {
        return -EINVAL;
    }
    if (!task || !task->mm) {
        return 0;
    }

    for (uint32_t i = 0; i < task->mm->vma_count; i++) {
        const struct task_vma *vma = &task->mm->vmas[i];
        uint64_t run_start = vma->start;
        uint32_t run_flags = task_vma_page_flags_impl(vma, run_start);
        char path[MAX_PATH];

        if (vfs_maps_path_for_vma(vma, path, sizeof(path)) != 0 || path[0] == '\0') {
            const char *kind = vfs_maps_kind_name(vma);
            size_t kind_len = strlen(kind);
            if (kind_len >= sizeof(path)) {
                kind_len = sizeof(path) - 1;
            }
            memcpy(path, kind, kind_len);
            path[kind_len] = '\0';
        }

        for (uint64_t addr = vma->start + TASK_VMA_PAGE_SIZE; addr < vma->end; addr += TASK_VMA_PAGE_SIZE) {
            uint32_t flags = task_vma_page_flags_impl(vma, addr);
            if (flags == run_flags) {
                continue;
            }
            char perms[5];
            uint64_t offset = vma->backing_offset + (run_start - vma->start);
            vfs_maps_permissions(run_flags, vma->shared, perms);
            if (vfs_proc_append(buf, buf_len, &pos,
                                "%012llx-%012llx %s %08llx 00:00 0%s%s\n",
                                (unsigned long long)run_start,
                                (unsigned long long)addr,
                                perms,
                                (unsigned long long)offset,
                                path[0] ? " " : "",
                                path) != 0) {
                return (int)pos;
            }
            run_start = addr;
            run_flags = flags;
        }
        char perms[5];
        uint64_t offset = vma->backing_offset + (run_start - vma->start);
        vfs_maps_permissions(run_flags, vma->shared, perms);
        if (vfs_proc_append(buf, buf_len, &pos,
                            "%012llx-%012llx %s %08llx 00:00 0%s%s\n",
                            (unsigned long long)run_start,
                            (unsigned long long)vma->end,
                            perms,
                            (unsigned long long)offset,
                            path[0] ? " " : "",
                            path) != 0) {
            return (int)pos;
        }
    }
    return (int)pos;
}

int vfs_proc_task_maps_content(int32_t pid, char *buf, size_t buf_len) {
    struct task *task = vfs_task_for_proc_pid(pid);
    int ret;

    if (!task) {
        return -ESRCH;
    }
    ret = vfs_proc_task_maps_content_for_task(task, buf, buf_len);
    vfs_put_proc_task(task);
    return ret;
}

int vfs_proc_self_maps_content(char *buf, size_t buf_len) {
    struct task *task = task_current();
    return vfs_proc_task_maps_content(task ? task->pid : -1, buf, buf_len);
}

static uint64_t vfs_vma_resident_page_count(const struct task_vma *vma) {
    uint64_t resident = 0;

    if (!vma) {
        return 0;
    }
    if (!vma->resident_pages) {
        return vma->page_count;
    }
    for (uint64_t page = 0; page < vma->page_count; page++) {
        if (vma->resident_pages[page]) {
            resident++;
        }
    }
    return resident;
}

static uint64_t vfs_vma_dirty_page_count_range(const struct task_vma *vma,
                                               uint64_t first_page,
                                               uint64_t page_count) {
    uint64_t dirty = 0;

    if (!vma || !vma->dirty_pages || first_page >= vma->page_count) {
        return 0;
    }
    if (page_count > vma->page_count - first_page) {
        page_count = vma->page_count - first_page;
    }
    for (uint64_t page = 0; page < page_count; page++) {
        if (vma->dirty_pages[first_page + page]) {
            dirty++;
        }
    }
    return dirty;
}

static uint64_t vfs_vma_resident_page_count_range(const struct task_vma *vma,
                                                  uint64_t first_page,
                                                  uint64_t page_count) {
    uint64_t resident = 0;

    if (!vma || first_page >= vma->page_count) {
        return 0;
    }
    if (page_count > vma->page_count - first_page) {
        page_count = vma->page_count - first_page;
    }
    if (!vma->resident_pages) {
        return page_count;
    }
    for (uint64_t page = 0; page < page_count; page++) {
        if (vma->resident_pages[first_page + page]) {
            resident++;
        }
    }
    return resident;
}

static int vfs_vma_vmflags_for_page_flags(const struct task_vma *vma, uint32_t flags,
                                           char *buf, size_t buf_len) {
    size_t pos = 0;
    int ret;

    if (!vma || !buf || buf_len == 0) {
        return -EINVAL;
    }

    buf[0] = '\0';

    if ((flags & PF_R) != 0) {
        ret = snprintf(buf + pos, buf_len - pos, "%srd", pos ? " " : "");
        if (ret < 0 || (size_t)ret >= buf_len - pos) {
            return -ENAMETOOLONG;
        }
        pos += (size_t)ret;
    }
    if ((flags & PF_W) != 0) {
        ret = snprintf(buf + pos, buf_len - pos, "%swr", pos ? " " : "");
        if (ret < 0 || (size_t)ret >= buf_len - pos) {
            return -ENAMETOOLONG;
        }
        pos += (size_t)ret;
    }
    if ((flags & PF_X) != 0) {
        ret = snprintf(buf + pos, buf_len - pos, "%sex", pos ? " " : "");
        if (ret < 0 || (size_t)ret >= buf_len - pos) {
            return -ENAMETOOLONG;
        }
        pos += (size_t)ret;
    }
    ret = snprintf(buf + pos, buf_len - pos, "%s%smr mw me ac",
                   pos ? " " : "", vma->shared ? "sh " : "");
    if (ret < 0 || (size_t)ret >= buf_len - pos) {
        return -ENAMETOOLONG;
    }
    return 0;
}

static int vfs_vma_vmflags(const struct task_vma *vma, char *buf, size_t buf_len) {
    if (!vma) {
        return -EINVAL;
    }
    return vfs_vma_vmflags_for_page_flags(vma, task_vma_page_flags_impl(vma, vma->start),
                                          buf, buf_len);
}

static int vfs_proc_task_smaps_content_for_task(struct task *task, char *buf, size_t buf_len) {
    size_t pos = 0;

    if (!buf || buf_len == 0) {
        return -EINVAL;
    }
    if (!task || !task->mm) {
        return 0;
    }

    for (uint32_t i = 0; i < task->mm->vma_count; i++) {
        const struct task_vma *vma = &task->mm->vmas[i];
        char path[MAX_PATH];
        char vmflags[64];
        uint64_t run_start;
        uint32_t run_flags;

        if (vfs_vma_vmflags(vma, vmflags, sizeof(vmflags)) != 0) {
            vmflags[0] = '\0';
        }
        if (vfs_maps_path_for_vma(vma, path, sizeof(path)) != 0 || path[0] == '\0') {
            const char *kind = vfs_maps_kind_name(vma);
            size_t kind_len = strlen(kind);
            if (kind_len >= sizeof(path)) {
                kind_len = sizeof(path) - 1;
            }
            memcpy(path, kind, kind_len);
            path[kind_len] = '\0';
        }

        run_start = vma->start;
        run_flags = task_vma_page_flags_impl(vma, run_start);
        for (uint64_t addr = vma->start + TASK_VMA_PAGE_SIZE; addr <= vma->end; addr += TASK_VMA_PAGE_SIZE) {
            uint32_t flags = addr < vma->end ? task_vma_page_flags_impl(vma, addr) : run_flags ^ U32_MAX;
            if (addr < vma->end && flags == run_flags) {
                continue;
            }

            char perms[5];
            uint64_t first_page = (run_start - vma->start) / TASK_VMA_PAGE_SIZE;
            uint64_t page_count = (addr - run_start) / TASK_VMA_PAGE_SIZE;
            uint64_t size_kb = page_count * 4ULL;
            uint64_t resident_kb = vfs_vma_resident_page_count_range(vma, first_page, page_count) * 4ULL;
            uint64_t dirty_kb = vfs_vma_dirty_page_count_range(vma, first_page, page_count) * 4ULL;
            uint64_t shared_clean_kb = vma->shared ? (size_kb > dirty_kb ? size_kb - dirty_kb : 0) : 0;
            uint64_t private_clean_kb = vma->shared ? 0 : (size_kb > dirty_kb ? size_kb - dirty_kb : 0);
            uint64_t shared_dirty_kb = vma->shared ? dirty_kb : 0;
            uint64_t private_dirty_kb = vma->shared ? 0 : dirty_kb;
            uint64_t anonymous_kb = 0;

            if (vma->kind == TASK_VMA_ANON || vma->kind == TASK_VMA_STACK) {
                anonymous_kb = size_kb;
            } else if (vma->kind == TASK_VMA_FILE && !vma->shared) {
                anonymous_kb = dirty_kb;
            }

            if (vfs_vma_vmflags_for_page_flags(vma, run_flags, vmflags, sizeof(vmflags)) != 0) {
                vmflags[0] = '\0';
            }
            vfs_maps_permissions(run_flags, vma->shared, perms);
            if (vfs_proc_append(buf, buf_len, &pos,
                                "%012llx-%012llx %s %08llx 00:00 0%s%s\n",
                                (unsigned long long)run_start,
                                (unsigned long long)addr,
                                perms,
                                (unsigned long long)(vma->backing_offset + (run_start - vma->start)),
                                path[0] ? " " : "",
                                path) != 0 ||
                vfs_proc_append(buf, buf_len, &pos, "Size:           %8llu kB\n", (unsigned long long)size_kb) != 0 ||
                vfs_proc_append(buf, buf_len, &pos, "KernelPageSize: %8llu kB\n", 4ULL) != 0 ||
                vfs_proc_append(buf, buf_len, &pos, "MMUPageSize:    %8llu kB\n", 4ULL) != 0 ||
                vfs_proc_append(buf, buf_len, &pos, "Rss:            %8llu kB\n", (unsigned long long)resident_kb) != 0 ||
                vfs_proc_append(buf, buf_len, &pos, "Pss:            %8llu kB\n", (unsigned long long)resident_kb) != 0 ||
                vfs_proc_append(buf, buf_len, &pos, "Shared_Clean:   %8llu kB\n", (unsigned long long)shared_clean_kb) != 0 ||
                vfs_proc_append(buf, buf_len, &pos, "Shared_Dirty:   %8llu kB\n", (unsigned long long)shared_dirty_kb) != 0 ||
                vfs_proc_append(buf, buf_len, &pos, "Private_Clean:  %8llu kB\n", (unsigned long long)private_clean_kb) != 0 ||
                vfs_proc_append(buf, buf_len, &pos, "Private_Dirty:  %8llu kB\n", (unsigned long long)private_dirty_kb) != 0 ||
                vfs_proc_append(buf, buf_len, &pos, "Referenced:     %8llu kB\n", (unsigned long long)resident_kb) != 0 ||
                vfs_proc_append(buf, buf_len, &pos, "Anonymous:      %8llu kB\n", (unsigned long long)anonymous_kb) != 0 ||
                vfs_proc_append(buf, buf_len, &pos, "LazyFree:       %8llu kB\n", 0ULL) != 0 ||
                vfs_proc_append(buf, buf_len, &pos, "AnonHugePages:  %8llu kB\n", 0ULL) != 0 ||
                vfs_proc_append(buf, buf_len, &pos, "ShmemPmdMapped: %8llu kB\n", 0ULL) != 0 ||
                vfs_proc_append(buf, buf_len, &pos, "FilePmdMapped:  %8llu kB\n", 0ULL) != 0 ||
                vfs_proc_append(buf, buf_len, &pos, "Shared_Hugetlb: %8llu kB\n", 0ULL) != 0 ||
                vfs_proc_append(buf, buf_len, &pos, "Private_Hugetlb:%8llu kB\n", 0ULL) != 0 ||
                vfs_proc_append(buf, buf_len, &pos, "Swap:           %8llu kB\n", 0ULL) != 0 ||
                vfs_proc_append(buf, buf_len, &pos, "SwapPss:        %8llu kB\n", 0ULL) != 0 ||
                vfs_proc_append(buf, buf_len, &pos, "Locked:         %8llu kB\n", 0ULL) != 0 ||
                vfs_proc_append(buf, buf_len, &pos, "THPeligible:    %8llu\n", 0ULL) != 0 ||
                vfs_proc_append(buf, buf_len, &pos, "ProtectionKey:  %8llu\n", 0ULL) != 0 ||
                vfs_proc_append(buf, buf_len, &pos, "VmFlags: %s\n", vmflags) != 0) {
                return (int)pos;
            }
            run_start = addr;
            run_flags = flags;
        }
    }
    return (int)pos;
}

int vfs_proc_task_smaps_content(int32_t pid, char *buf, size_t buf_len) {
    struct task *task = vfs_task_for_proc_pid(pid);
    int ret;

    if (!task) {
        return -ESRCH;
    }
    ret = vfs_proc_task_smaps_content_for_task(task, buf, buf_len);
    vfs_put_proc_task(task);
    return ret;
}

int vfs_proc_self_smaps_content(char *buf, size_t buf_len) {
    struct task *task = task_current();
    return vfs_proc_task_smaps_content(task ? task->pid : -1, buf, buf_len);
}

int vfs_proc_self_fdinfo_content(int fd_num, char *buf, size_t buf_len) {
    struct task *task = task_current();
    return vfs_proc_task_fdinfo_content(task ? task->pid : -1, fd_num, buf, buf_len);
}

int vfs_proc_task_fdinfo_content(int32_t pid, int fd_num, char *buf, size_t buf_len) {
    struct task *task;
    void *entry;
    char path[MAX_PATH];
    __kernel_off_t offset;
    int flags;
    int fd_flags;
    int ret;
    size_t pos;
    bool is_current_task;

    if (!buf || buf_len == 0) {
        return -EINVAL;
    }

    if (fd_num < 0 || fd_num >= NR_OPEN_DEFAULT) {
        return -ENOENT;
    }

    task = vfs_task_for_proc_pid(pid);
    if (!task) {
        return -ESRCH;
    }
    is_current_task = task == task_current();
    if (!is_current_task) {
        unsigned long long mnt_id = 1;

        if (fdtable_task_fd_path_impl(task, fd_num, path, sizeof(path)) == 0) {
            mnt_id = vfs_proc_fdinfo_mount_id_for_path(task->fs, path);
        }
        ret = fdtable_task_fdinfo_content_impl(task, fd_num, mnt_id, buf, buf_len);
        vfs_put_proc_task(task);
        if (ret >= 0) {
            return ret;
        }
        return -ENOENT;
    }
    vfs_put_proc_task(task);

    entry = get_fd_entry_impl(fd_num);
    if (!entry) {
        return -ENOENT;
    }

    if (get_fd_path_impl(entry, path, sizeof(path)) != 0) {
        path[0] = '\0';
    }
    offset = get_fd_offset_impl(entry);
    flags = get_fd_flags_impl(entry);
    fd_flags = get_fd_descriptor_flags_impl(entry);
    bool is_epoll = get_fd_is_epoll_impl(entry);
    struct epoll_instance *epoll_instance = is_epoll ? get_fd_epoll_instance_impl(entry) : NULL;
    put_fd_entry_impl(entry);

    if (fd_flags & FD_CLOEXEC) {
        flags |= O_CLOEXEC;
    }

    ret = snprintf(buf, buf_len, "pos:\t%lld\nflags:\t0%o\nmnt_id:\t%llu\n",
                   (long long)offset, flags,
                   vfs_proc_fdinfo_mount_id_for_path(task_current() ? task_current()->fs : NULL, path));

    if (ret < 0) {
        return -EINVAL;
    }
    if ((size_t)ret >= buf_len) {
        return (int)(buf_len - 1);
    }
    pos = (size_t)ret;
    if (is_epoll) {
        int epoll_ret = epoll_fdinfo_content_impl(epoll_instance, buf, buf_len, &pos);
        if (epoll_ret != 0 && epoll_ret != -ENOSPC) {
            return epoll_ret;
        }
        if (epoll_ret == -ENOSPC) {
            return (int)pos;
        }
    }
    return (int)pos;
}

static int vfs_proc_append(char *buf, size_t buf_len, size_t *pos, const char *fmt, ...) {
    va_list ap;
    int ret;
    size_t available;

    if (!buf || !pos || *pos >= buf_len) {
        return -ENOSPC;
    }

    available = buf_len - *pos;
    va_start(ap, fmt);
    ret = vsnprintf(buf + *pos, available, fmt, ap);
    va_end(ap);

    if (ret < 0) {
        return -EINVAL;
    }
    if ((size_t)ret >= available) {
        *pos = buf_len - 1;
        return -ENOSPC;
    }

    *pos += (size_t)ret;
    return 0;
}

static int vfs_proc_append_mount_optional_fields(struct vfs_mount_namespace *mnt_ns,
                                                 const struct vfs_mount_entry *entry,
                                                 char *buf, size_t buf_len, size_t *pos) {
    if (!entry || entry->propagation == 0 || entry->propagation == MS_PRIVATE) {
        return 0;
    }
    if (entry->propagation == MS_SHARED) {
        return vfs_proc_append(buf, buf_len, pos, " shared:%llu",
                               (unsigned long long)entry->peer_group_id);
    }
    if (entry->propagation == MS_SLAVE) {
        return vfs_proc_append(buf, buf_len, pos, " master:%llu",
                               (unsigned long long)entry->master_group_id);
    }
    if (entry->propagation == MS_UNBINDABLE) {
        return vfs_proc_append(buf, buf_len, pos, " unbindable");
    }
    return 0;
}

static int vfs_proc_mountinfo_content_for_namespace(struct vfs_mount_namespace *mnt_ns, char *buf, size_t buf_len) {
    size_t pos = 0;

    if (!buf || buf_len == 0) {
        return -EINVAL;
    }

    if (!mnt_ns) {
        return -ESRCH;
    }

    if (vfs_proc_append(buf, buf_len, &pos, "1 0 0:1 / / rw,relatime - orlix-root orlix-root rw\n") != 0) {
        return (int)pos;
    }

    fs_mutex_lock(&mnt_ns->lock);
    for (size_t i = 0; i < MAX_MOUNTS; i++) {
        struct vfs_mount_entry *entry = &mnt_ns->entries[i];
        uint64_t parent_id;

        if (!entry->active) {
            continue;
        }

        parent_id = vfs_mount_parent_id_locked(mnt_ns, i);

        const char *mode = (entry->flags & MNT_READONLY) ? "ro" : "rw";
        const char *fstype = entry->fstype[0] ? entry->fstype : "none";
        if (vfs_proc_append(buf, buf_len, &pos, "%llu %llu 0:%llu / %s %s,relatime",
                            (unsigned long long)entry->mount_id,
                            (unsigned long long)parent_id,
                            (unsigned long long)entry->mount_id,
                            entry->target, mode) != 0 ||
            vfs_proc_append_mount_optional_fields(mnt_ns, entry, buf, buf_len, &pos) != 0 ||
            vfs_proc_append(buf, buf_len, &pos, " - %s %s %s,bind\n",
                            fstype, entry->source, mode) != 0) {
            break;
        }
    }
    fs_mutex_unlock(&mnt_ns->lock);

    return (int)pos;
}

int vfs_proc_task_mountinfo_content(int32_t pid, char *buf, size_t buf_len) {
    struct task *task = vfs_task_for_proc_pid(pid);
    int ret;

    if (!task) {
        return -ESRCH;
    }
    ret = vfs_proc_mountinfo_content_for_namespace(task->fs ? task->fs->mnt_ns : NULL, buf, buf_len);
    vfs_put_proc_task(task);
    return ret;
}

int vfs_proc_self_mountinfo_content(char *buf, size_t buf_len) {
    struct task *task = task_current();
    return vfs_proc_task_mountinfo_content(task ? task->pid : -1, buf, buf_len);
}

static int vfs_proc_mounts_content_for_namespace(struct vfs_mount_namespace *mnt_ns, char *buf, size_t buf_len) {
    size_t pos = 0;

    if (!buf || buf_len == 0) {
        return -EINVAL;
    }

    if (!mnt_ns) {
        return -ESRCH;
    }

    if (vfs_proc_append(buf, buf_len, &pos, "orlix-root / orlix-root rw 0 0\n") != 0) {
        return (int)pos;
    }

    fs_mutex_lock(&mnt_ns->lock);
    for (size_t i = 0; i < MAX_MOUNTS; i++) {
        struct vfs_mount_entry *entry = &mnt_ns->entries[i];

        if (!entry->active) {
            continue;
        }

        const char *mode = (entry->flags & MNT_READONLY) ? "ro" : "rw";
        const char *fstype = entry->fstype[0] ? entry->fstype : "none";
        if (vfs_proc_append(buf, buf_len, &pos, "%s %s %s %s,bind 0 0\n",
                            entry->source, entry->target, fstype, mode) != 0) {
            break;
        }
    }
    fs_mutex_unlock(&mnt_ns->lock);

    return (int)pos;
}

int vfs_proc_task_mounts_content(int32_t pid, char *buf, size_t buf_len) {
    struct task *task = vfs_task_for_proc_pid(pid);
    int ret;

    if (!task) {
        return -ESRCH;
    }
    ret = vfs_proc_mounts_content_for_namespace(task->fs ? task->fs->mnt_ns : NULL, buf, buf_len);
    vfs_put_proc_task(task);
    return ret;
}

int vfs_proc_self_mounts_content(char *buf, size_t buf_len) {
    struct task *task = task_current();
    return vfs_proc_task_mounts_content(task ? task->pid : -1, buf, buf_len);
}

int vfs_proc_task_cgroup_content(int32_t pid, char *buf, size_t buf_len) {
    return cgroup_proc_task_content(pid, buf, buf_len);
}

int vfs_proc_filesystems_content(char *buf, size_t buf_len) {
    size_t pos = 0;

    if (!buf || buf_len == 0) {
        return -EINVAL;
    }
    if (vfs_proc_append(buf, buf_len, &pos,
                        "nodev\tsysfs\n"
                        "nodev\tproc\n"
                        "nodev\tdevtmpfs\n"
                        "nodev\tdevpts\n"
                        "nodev\ttmpfs\n"
                        "nodev\torlix-root\n") != 0) {
        return (int)pos;
    }
    return (int)pos;
}

int vfs_proc_meminfo_content(char *buf, size_t buf_len) {
    if (!buf || buf_len == 0) {
        return -EINVAL;
    }
    int ret = snprintf(buf, buf_len,
                       "MemTotal:         262144 kB\n"
                       "MemFree:          131072 kB\n"
                       "MemAvailable:     196608 kB\n"
                       "Buffers:               0 kB\n"
                       "Cached:            65536 kB\n"
                       "SwapCached:            0 kB\n"
                       "Active:                0 kB\n"
                       "Inactive:              0 kB\n"
                       "SwapTotal:             0 kB\n"
                       "SwapFree:              0 kB\n");
    if (ret < 0) {
        return -EINVAL;
    }
    if ((size_t)ret >= buf_len) {
        return (int)(buf_len - 1);
    }
    return ret;
}

int vfs_proc_cpuinfo_content(char *buf, size_t buf_len) {
    if (!buf || buf_len == 0) {
        return -EINVAL;
    }
    int ret = snprintf(buf, buf_len,
                       "processor\t: 0\n"
                       "BogoMIPS\t: 0.00\n"
                       "Features\t: fp asimd evtstrm aes pmull sha1 sha2 crc32 atomics\n"
                       "CPU implementer\t: 0x00\n"
                       "CPU architecture: AArch64\n"
                       "CPU variant\t: 0x0\n"
                       "CPU part\t: 0x000\n"
                       "CPU revision\t: 0\n");
    if (ret < 0) {
        return -EINVAL;
    }
    if ((size_t)ret >= buf_len) {
        return (int)(buf_len - 1);
    }
    return ret;
}

int vfs_proc_task_status_content(int32_t pid, char *buf, size_t buf_len) {
    struct task *task;
    struct cred *cred;
    struct vfs_vm_accounting acct;
    size_t pos = 0;
    char state_char;
    unsigned int queued_signals = 0;
    uint64_t sigpnd = 0;
    uint64_t shdpnd = 0;
    uint64_t sigblk = 0;
    uint64_t sigign = 0;
    uint64_t sigcgt = 0;
    unsigned int thread_count;

    if (!buf || buf_len == 0) {
        return -EINVAL;
    }

    task = vfs_task_for_proc_pid(pid);
    if (!task) {
        return -ESRCH;
    }

    cred = task->cred ? task->cred : get_current_cred();
    if (!cred) {
        vfs_put_proc_task(task);
        return -ESRCH;
    }

    switch (atomic_read(&task->state)) {
        case RUN_STATE_RUNNING:
            state_char = 'R';
            break;
        case RUN_STATE_INTERRUPTIBLE:
            state_char = 'S';
            break;
        case RUN_STATE_UNINTERRUPTIBLE:
            state_char = 'D';
            break;
        case RUN_STATE_STOPPED:
            state_char = 'T';
            break;
        case RUN_STATE_ZOMBIE:
            state_char = 'Z';
            break;
        default:
            state_char = 'R';
            break;
    }
    vfs_vm_account_task(task, &acct);
    vfs_proc_task_signal_status(task, &queued_signals, &sigpnd, &shdpnd, &sigblk, &sigign, &sigcgt);
    thread_count = vfs_proc_task_thread_count(task);

    if (vfs_proc_append(buf, buf_len, &pos,
        "Name:\t%s\n"
        "State:\t%c (%s)\n"
        "Tgid:\t%d\n"
        "Pid:\t%d\n"
        "PPid:\t%d\n"
        "TracerPid:\t0\n"
        "Uid:\t%u\t%u\t%u\t%u\n"
        "Gid:\t%u\t%u\t%u\t%u\n"
        "Groups:\t",
        task->comm,
        state_char,
        vfs_proc_task_state_name(atomic_read(&task->state)),
        task->tgid,
        task->pid,
        task->ppid,
        cred->uid, cred->euid, cred->suid, cred->fsuid,
        cred->gid, cred->egid, cred->sgid, cred->fsgid) != 0) {
        vfs_put_proc_task(task);
        return (int)pos;
    }

    for (size_t i = 0; i < cred->group_count; i++) {
        if (vfs_proc_append(buf, buf_len, &pos, "%u%s", cred->groups[i],
                            (i + 1 == cred->group_count) ? "" : " ") != 0) {
            vfs_put_proc_task(task);
            return (int)pos;
        }
    }

    if (vfs_proc_append(buf, buf_len, &pos,
        "\n"
        "NStgid:\t%d\n"
        "NSpid:\t%d\n"
        "NSpgid:\t%d\n"
        "NSsid:\t%d\n"
        "CapInh:\t%016llx\n"
        "CapPrm:\t%016llx\n"
        "CapEff:\t%016llx\n"
        "CapBnd:\t%016llx\n"
        "CapAmb:\t%016llx\n"
        "NoNewPrivs:\t%d\n"
        "Dumpable:\t%d\n"
        "Threads:\t%u\n"
        "SigQ:\t%u/1024\n"
        "SigPnd:\t%016llx\n"
        "ShdPnd:\t%016llx\n"
        "SigBlk:\t%016llx\n"
        "SigIgn:\t%016llx\n"
        "SigCgt:\t%016llx\n"
        "VmPeak:\t%llu kB\n"
        "VmHWM:\t%llu kB\n"
        "VmSize:\t%llu kB\n"
        "VmLck:\t0 kB\n"
        "VmPin:\t0 kB\n"
        "VmRSS:\t%llu kB\n"
        "RssAnon:\t%llu kB\n"
        "RssFile:\t%llu kB\n"
        "VmData:\t%llu kB\n"
        "VmStk:\t%llu kB\n"
        "VmExe:\t%llu kB\n"
        "VmSwap:\t0 kB\n"
        "VmDirty:\t%llu kB\n",
        task->ns_pid,
        task->ns_pid,
        task->pgid,
        task->sid,
        (unsigned long long)cred->cap_inheritable,
        (unsigned long long)cred->cap_permitted,
        (unsigned long long)cred->cap_effective,
        (unsigned long long)cred->cap_bounding,
        (unsigned long long)cred->cap_ambient,
        cred->no_new_privs ? 1 : 0,
        task->exec_dumpable ? 1 : 0,
        thread_count,
        queued_signals,
        (unsigned long long)sigpnd,
        (unsigned long long)shdpnd,
        (unsigned long long)sigblk,
        (unsigned long long)sigign,
        (unsigned long long)sigcgt,
        (unsigned long long)(task->mm ? task->mm->vm_peak_pages * 4ULL : 0ULL),
        (unsigned long long)(task->mm ? task->mm->vm_high_water_rss_pages * 4ULL : 0ULL),
        (unsigned long long)(acct.size_pages * 4ULL),
        (unsigned long long)(acct.resident_pages * 4ULL),
        (unsigned long long)((acct.resident_pages - acct.resident_shared_pages) * 4ULL),
        (unsigned long long)(acct.resident_shared_pages * 4ULL),
        (unsigned long long)(acct.data_pages * 4ULL),
        (unsigned long long)(acct.stack_pages * 4ULL),
        (unsigned long long)(acct.text_pages * 4ULL),
        (unsigned long long)(acct.dirty_pages * 4ULL)) != 0) {
        vfs_put_proc_task(task);
        return (int)pos;
    }
    vfs_put_proc_task(task);
    return (int)pos;
}

int vfs_proc_self_status_content(char *buf, size_t buf_len) {
    struct task *task = task_current();
    return vfs_proc_task_status_content(task ? task->pid : -1, buf, buf_len);
}

static int vfs_proc_task_id_map_content(int32_t pid, char *buf, size_t buf_len, bool group_map) {
    struct task *task;
    struct cred *cred;
    int ret;

    if (!buf || buf_len == 0) {
        return -EINVAL;
    }

    task = vfs_task_for_proc_pid(pid);
    if (!task) {
        return -ESRCH;
    }

    cred = task->cred ? task->cred : get_current_cred();
    if (!cred) {
        vfs_put_proc_task(task);
        return -ESRCH;
    }
    ret = snprintf(buf, buf_len, "%10u %10u %10u\n",
                   group_map ? cred->gid_map_inside : cred->uid_map_inside,
                   group_map ? cred->gid_map_outside : cred->uid_map_outside,
                   group_map ? cred->gid_map_count : cred->uid_map_count);
    vfs_put_proc_task(task);
    if (ret < 0) {
        return -EINVAL;
    }
    if ((size_t)ret >= buf_len) {
        return (int)(buf_len - 1);
    }
    return ret;
}

int vfs_proc_task_uid_map_content(int32_t pid, char *buf, size_t buf_len) {
    return vfs_proc_task_id_map_content(pid, buf, buf_len, false);
}

int vfs_proc_task_gid_map_content(int32_t pid, char *buf, size_t buf_len) {
    return vfs_proc_task_id_map_content(pid, buf, buf_len, true);
}

int vfs_proc_task_setgroups_content(int32_t pid, char *buf, size_t buf_len) {
    struct task *task;
    struct cred *cred;
    int ret;

    if (!buf || buf_len == 0) {
        return -EINVAL;
    }
    task = vfs_task_for_proc_pid(pid);
    if (!task) {
        return -ESRCH;
    }
    cred = task->cred ? task->cred : get_current_cred();
    if (!cred) {
        vfs_put_proc_task(task);
        return -ESRCH;
    }
    ret = snprintf(buf, buf_len, "%s\n", cred_setgroups_state(cred));
    vfs_put_proc_task(task);
    if (ret < 0) {
        return -EINVAL;
    }
    if ((size_t)ret >= buf_len) {
        return (int)(buf_len - 1);
    }
    return ret;
}

long vfs_proc_task_write_id_map_content(synthetic_proc_file_t proc_file, int32_t pid,
                                        const char *buf, size_t count) {
    struct task *current = task_current();
    struct task *task;
    int ret;

    if (!buf && count > 0) {
        return -EFAULT;
    }
    if (!current) {
        return -ESRCH;
    }
    task = vfs_task_for_proc_pid(pid);
    if (!task) {
        return -ESRCH;
    }
    if (task->pid != current->pid) {
        vfs_put_proc_task(task);
        return -EPERM;
    }
    if (!task->cred) {
        vfs_put_proc_task(task);
        return -ESRCH;
    }
    switch (proc_file) {
    case SYNTHETIC_PROC_FILE_UID_MAP:
        ret = cred_write_uid_map(task->cred, buf, count);
        break;
    case SYNTHETIC_PROC_FILE_GID_MAP:
        ret = cred_write_gid_map(task->cred, buf, count);
        break;
    case SYNTHETIC_PROC_FILE_SETGROUPS:
        ret = cred_write_setgroups(task->cred, buf, count);
        break;
    default:
        ret = -EINVAL;
        break;
    }
    vfs_put_proc_task(task);
    return ret == 0 ? (long)count : ret;
}

int vfs_access(const char *pathname, int mode) {
    if (!pathname) {
        return -EFAULT;
    }

    return vfs_faccessat(AT_FDCWD, pathname, mode, 0);
}

int vfs_path_exists(const char *pathname) {
    struct stat st;
    int ret;

    ret = vfs_fstatat(AT_FDCWD, pathname, &st, 0);
    if (ret == 0) {
        return 1;
    }
    if (ret == -ENOENT) {
        return 0;
    }
    return ret;
}

int vfs_fstatat(int dirfd, const char *pathname, struct stat *statbuf, int flags) {
    char translated_path[MAX_PATH];
    char resolved_virtual[MAX_PATH];
    int ret;
    bool follow_symlink;
    int supported_flags = AT_SYMLINK_NOFOLLOW;

    if (!pathname || !statbuf) {
        return -EFAULT;
    }

    if (flags & ~supported_flags) {
        return -EINVAL;
    }

    follow_symlink = !(flags & AT_SYMLINK_NOFOLLOW);

    ret = vfs_resolve_virtual_path_at_follow(dirfd, pathname, resolved_virtual,
                                             sizeof(resolved_virtual), follow_symlink);
    if (ret != 0) {
        return ret;
    }

    if (vfs_path_is_synthetic_root(resolved_virtual)) {
        memset(statbuf, 0, sizeof(*statbuf));
        statbuf->st_mode = S_IFDIR | 0555;
        statbuf->st_nlink = 2;
        statbuf->st_uid = 0;
        statbuf->st_gid = 0;
        statbuf->st_size = 0;
        statbuf->st_blksize = 4096;
        statbuf->st_blocks = 0;
        return 0;
    }

    if (strcmp(resolved_virtual, "/sys/fs") == 0 ||
        cgroupfs_classify_path(resolved_virtual) == CGROUPFS_NODE_DIR) {
        memset(statbuf, 0, sizeof(*statbuf));
        statbuf->st_mode = S_IFDIR | 0555;
        statbuf->st_nlink = 2;
        statbuf->st_uid = 0;
        statbuf->st_gid = 0;
        statbuf->st_size = 0;
        statbuf->st_blksize = 4096;
        statbuf->st_blocks = 0;
        return 0;
    }
    if (cgroupfs_classify_path(resolved_virtual) != CGROUPFS_NODE_NONE) {
        memset(statbuf, 0, sizeof(*statbuf));
        statbuf->st_mode = S_IFREG | 0644;
        statbuf->st_nlink = 1;
        statbuf->st_uid = 0;
        statbuf->st_gid = 0;
        statbuf->st_size = 0;
        statbuf->st_blksize = 4096;
        statbuf->st_blocks = 0;
        return 0;
    }

    if (vfs_path_is_synthetic_dev_dir(resolved_virtual)) {
        memset(statbuf, 0, sizeof(*statbuf));
        statbuf->st_mode = S_IFDIR | 0555;
        statbuf->st_nlink = 2;
        statbuf->st_uid = 0;
        statbuf->st_gid = 0;
        statbuf->st_size = 0;
        statbuf->st_blksize = 4096;
        statbuf->st_blocks = 0;
        return 0;
    }

    if (strcmp(resolved_virtual, "/tmp") == 0 || strcmp(resolved_virtual, "/var/tmp") == 0 ||
        strcmp(resolved_virtual, "/run") == 0 || strcmp(resolved_virtual, "/var/cache") == 0) {
        memset(statbuf, 0, sizeof(*statbuf));
        statbuf->st_mode = S_IFDIR | 0777;
        statbuf->st_nlink = 2;
        statbuf->st_uid = 0;
        statbuf->st_gid = 0;
        statbuf->st_size = 0;
        statbuf->st_blksize = 4096;
        statbuf->st_blocks = 0;
        vfs_apply_stat_metadata(resolved_virtual, statbuf);
        return 0;
    }

    {
        synthetic_dev_node_t dev_node = vfs_path_is_synthetic_dev_node(resolved_virtual);
        if (dev_node != SYNTHETIC_DEV_NONE) {
            memset(statbuf, 0, sizeof(*statbuf));
            statbuf->st_mode = S_IFCHR | 0666;
            statbuf->st_nlink = 1;
            statbuf->st_uid = 0;
            statbuf->st_gid = 0;
            statbuf->st_rdev = (dev_node == SYNTHETIC_DEV_NULL) ? makedev(1, 3) :
                               (dev_node == SYNTHETIC_DEV_ZERO) ? makedev(1, 5) :
                               (dev_node == SYNTHETIC_DEV_RANDOM) ? makedev(1, 8) :
                               (dev_node == SYNTHETIC_DEV_URANDOM) ? makedev(1, 9) :
                               makedev(5, 2);
            statbuf->st_blksize = 4096;
            statbuf->st_blocks = 0;
            return 0;
        }
    }

    {
        unsigned int pty_index = 0;
        if (pty_lookup_slave_path_impl(resolved_virtual, &pty_index) == 0) {
            memset(statbuf, 0, sizeof(*statbuf));
            statbuf->st_mode = S_IFCHR | 0666;
            statbuf->st_nlink = 1;
            statbuf->st_uid = 0;
            statbuf->st_gid = 0;
            statbuf->st_rdev = makedev(136, pty_index);
            statbuf->st_blksize = 4096;
            statbuf->st_blocks = 0;
            return 0;
        }
    }

    if (strcmp(resolved_virtual, "/dev/tty") == 0) {
        memset(statbuf, 0, sizeof(*statbuf));
        statbuf->st_mode = S_IFCHR | 0666;
        statbuf->st_nlink = 1;
        statbuf->st_uid = 0;
        statbuf->st_gid = 0;
        statbuf->st_rdev = makedev(5, 0);
        statbuf->st_blksize = 4096;
        statbuf->st_blocks = 0;
        return 0;
    }

    {
        proc_self_path_class_t proc_class = vfs_classify_proc_self_path(resolved_virtual);
        if (proc_class == PROC_SELF_DIR || proc_class == PROC_SELF_FD_DIR ||
            proc_class == PROC_SELF_FDINFO_DIR || proc_class == PROC_SELF_NS_DIR ||
            proc_class == PROC_SELF_TASK_DIR || proc_class == PROC_SELF_THREAD_DIR) {
            memset(statbuf, 0, sizeof(*statbuf));
            statbuf->st_mode = S_IFDIR | 0555;
            statbuf->st_nlink = 2;
            statbuf->st_uid = 0;
            statbuf->st_gid = 0;
            statbuf->st_size = 0;
            statbuf->st_blksize = 4096;
            statbuf->st_blocks = 0;
            return 0;
        } else if (proc_class == PROC_SELF_FD_LINK) {
            char link_target[MAX_PATH];
            ret = vfs_proc_self_fd_link_target(resolved_virtual, link_target, sizeof(link_target));
            if (ret != 0) {
                return ret;
            }
            memset(statbuf, 0, sizeof(*statbuf));
            statbuf->st_mode = S_IFLNK | 0777;
            statbuf->st_nlink = 1;
            statbuf->st_uid = 0;
            statbuf->st_gid = 0;
            statbuf->st_size = strlen(link_target);
            statbuf->st_blksize = 4096;
            statbuf->st_blocks = 0;
            return 0;
        } else if (proc_class == PROC_SELF_CWD_LINK) {
            char link_target[MAX_PATH];
            ret = vfs_proc_task_cwd_target(vfs_proc_target_pid_for_path(resolved_virtual),
                                           link_target, sizeof(link_target));
            if (ret != 0) {
                return ret;
            }
            memset(statbuf, 0, sizeof(*statbuf));
            statbuf->st_mode = S_IFLNK | 0777;
            statbuf->st_nlink = 1;
            statbuf->st_uid = 0;
            statbuf->st_gid = 0;
            statbuf->st_size = strlen(link_target);
            statbuf->st_blksize = 4096;
            statbuf->st_blocks = 0;
            return 0;
        } else if (proc_class == PROC_SELF_EXE_LINK) {
            char link_target[MAX_PATH];
            ret = vfs_proc_task_exe_target(vfs_proc_target_pid_for_path(resolved_virtual),
                                           link_target, sizeof(link_target));
            if (ret != 0) {
                return ret;
            }
            memset(statbuf, 0, sizeof(*statbuf));
            statbuf->st_mode = S_IFLNK | 0777;
            statbuf->st_nlink = 1;
            statbuf->st_uid = 0;
            statbuf->st_gid = 0;
            statbuf->st_size = strlen(link_target);
            statbuf->st_blksize = 4096;
            statbuf->st_blocks = 0;
            return 0;
        } else if (proc_class == PROC_SELF_NS_LINK) {
            char link_target[MAX_PATH];
            ret = vfs_proc_self_ns_link_target(resolved_virtual, link_target, sizeof(link_target));
            if (ret != 0) {
                return ret;
            }
            memset(statbuf, 0, sizeof(*statbuf));
            statbuf->st_mode = S_IFLNK | 0777;
            statbuf->st_nlink = 1;
            statbuf->st_uid = 0;
            statbuf->st_gid = 0;
            statbuf->st_size = strlen(link_target);
            statbuf->st_blksize = 4096;
            statbuf->st_blocks = 0;
            return 0;
        } else if (proc_class == PROC_SELF_CMDLINE_FILE || proc_class == PROC_SELF_ENVIRON_FILE ||
                   proc_class == PROC_SELF_COMM_FILE ||
                   proc_class == PROC_SELF_STAT_FILE || proc_class == PROC_SELF_STATM_FILE ||
                   proc_class == PROC_SELF_MAPS_FILE || proc_class == PROC_SELF_SMAPS_FILE ||
                   proc_class == PROC_SELF_STATUS_FILE || proc_class == PROC_SELF_CGROUP_FILE ||
                   proc_class == PROC_SELF_UID_MAP_FILE || proc_class == PROC_SELF_GID_MAP_FILE ||
                   proc_class == PROC_SELF_SETGROUPS_FILE ||
                   proc_class == PROC_SELF_MOUNTINFO_FILE ||
                   proc_class == PROC_SELF_MOUNTS_FILE || proc_class == PROC_ROOT_FILESYSTEMS_FILE ||
                   proc_class == PROC_ROOT_MEMINFO_FILE || proc_class == PROC_ROOT_CPUINFO_FILE) {
            memset(statbuf, 0, sizeof(*statbuf));
            statbuf->st_mode = S_IFREG | 0444;
            statbuf->st_nlink = 1;
            statbuf->st_uid = 0;
            statbuf->st_gid = 0;
            statbuf->st_size = 0;
            statbuf->st_blksize = 4096;
            statbuf->st_blocks = 0;
            return 0;
        } else if (proc_class == PROC_SELF_FDINFO_FILE) {
            int fd_num = vfs_proc_fd_num_for_path(resolved_virtual, "/fdinfo/");
            if (fd_num < 0) {
                return -ENOENT;
            }
            if (!vfs_proc_fd_exists_for_path(resolved_virtual, fd_num)) {
                return -ENOENT;
            }
            memset(statbuf, 0, sizeof(*statbuf));
            statbuf->st_mode = S_IFREG | 0444;
            statbuf->st_nlink = 1;
            statbuf->st_uid = 0;
            statbuf->st_gid = 0;
            statbuf->st_size = 0;
            statbuf->st_blksize = 4096;
            statbuf->st_blocks = 0;
            return 0;
        }
    }

    if (vfs_path_is_synthetic(resolved_virtual)) {
        return -ENOENT;
    }

    /* Translate the resolved virtual path to host backing path */
    ret = vfs_join_backing_root(resolved_virtual, translated_path, sizeof(translated_path));
    if (ret != 0) {
        return ret;
    }

    if (follow_symlink) {
        ret = vfs_stat_path(translated_path, statbuf);
    } else {
        ret = vfs_lstat(translated_path, statbuf);
    }
    if (ret != 0) {
        return ret;
    }
    vfs_apply_stat_metadata(resolved_virtual, statbuf);
    return 0;
}

int vfs_faccessat(int dirfd, const char *pathname, int mode, int flags) {
    char translated_path[MAX_PATH];
    char resolved_virtual[MAX_PATH];
    int ret;

    if (!pathname) {
        return -EFAULT;
    }

    if (flags & ~(AT_EACCESS | AT_SYMLINK_NOFOLLOW)) {
        return -EINVAL;
    }

    if (flags & AT_SYMLINK_NOFOLLOW) {
        return -EOPNOTSUPP;
    }

    ret = vfs_resolve_virtual_path_at_follow(dirfd, pathname, resolved_virtual,
                                             sizeof(resolved_virtual), true);
    if (ret != 0) {
        return ret;
    }

    if (vfs_path_is_synthetic_root(resolved_virtual)) {
        return 0;
    }

    if (strcmp(resolved_virtual, "/sys/fs") == 0 ||
        cgroupfs_classify_path(resolved_virtual) != CGROUPFS_NODE_NONE) {
        return 0;
    }

    if (vfs_path_is_synthetic_dev_node(resolved_virtual) != SYNTHETIC_DEV_NONE) {
        return 0;
    }

    if (strcmp(resolved_virtual, "/dev/tty") == 0 || vfs_path_is_synthetic_dev_dir(resolved_virtual)) {
        return 0;
    }

    if (pty_is_virtual_slave_path_impl(resolved_virtual)) {
        return 0;
    }

    if (vfs_classify_proc_self_path(resolved_virtual) != PROC_SELF_NONE) {
        return 0;
    }

    if (vfs_path_is_synthetic(resolved_virtual)) {
        return -ENOENT;
    }

    ret = vfs_translate_path(resolved_virtual, translated_path, sizeof(translated_path));
    if (ret != 0) {
        return ret;
    }

    {
        struct stat st;
        struct cred *cred = get_current_cred();
        uint32_t check_uid;
        uint32_t check_gid;

        ret = vfs_stat_virtual_backed_path(resolved_virtual, translated_path, &st);
        if (ret != 0) {
            return ret;
        }

        if ((flags & AT_EACCESS) != 0) {
            check_uid = cred ? cred->euid : (uint32_t)-1;
            check_gid = cred ? cred->egid : (uint32_t)-1;
        } else {
            check_uid = cred ? cred->uid : (uint32_t)-1;
            check_gid = cred ? cred->gid : (uint32_t)-1;
        }

        if ((mode & 4) != 0 && !vfs_cred_has_mode_permission_as(cred, &st, 04U, check_uid, check_gid)) {
            return -EACCES;
        }
        if ((mode & 2) != 0 && !vfs_cred_has_mode_permission_as(cred, &st, 02U, check_uid, check_gid)) {
            return -EACCES;
        }
        if ((mode & 1) != 0 && !vfs_cred_has_mode_permission_as(cred, &st, 01U, check_uid, check_gid)) {
            return -EACCES;
        }
    }

    return 0;
}

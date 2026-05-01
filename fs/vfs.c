#include "vfs.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Linux UAPI headers for ABI constants */
#include <linux/capability.h>
#include <linux/fcntl.h>
#include <linux/mount.h>
#include <linux/stat.h>

/* Narrow seam headers for host operations */
#include "internal/ios/fs/file_io_host.h"
#include "internal/ios/fs/path_host.h"
#include "internal/ios/fs/path_discovery_host.h"

#include "fdtable.h"
#include "pty.h"
#include "../kernel/task.h"
#include "../kernel/cred_internal.h"
#include "../kernel/uts.h"

/* makedev for device nodes - Linux UAPI style */
#ifndef makedev
#define makedev(major, minor) ((((major) & 0xfff) << 8) | ((minor) & 0xff))
#endif

static const char *vfs_virtual_root_path = "/";

static int vfs_ensure_backing_initialized(void);
static int vfs_join_virtual_path(const char *base_path, const char *suffix, char *joined_path,
                                 size_t joined_path_len);
static int vfs_stat_virtual_backed_path(const char *resolved_vpath, const char *translated_path,
                                        struct linux_stat *st);
static bool vfs_path_is_on_readonly_mount(const char *resolved_vpath);

/* Backing storage class roots - discovered at runtime from host container */
static char vfs_persistent_root[MAX_PATH] = {0};
static char vfs_cache_root[MAX_PATH] = {0};
static char vfs_temp_root[MAX_PATH] = {0};
static int vfs_backing_initialized = 0;
static int vfs_etc_bootstrapped = 0;

struct vfs_mount_entry {
    bool active;
    char source[MAX_PATH];
    char target[MAX_PATH];
    char fstype[32];
    unsigned long flags;
};

struct vfs_mount_namespace {
    struct vfs_mount_entry entries[MAX_MOUNTS];
    linux_atomic_int refs;
    uint64_t ns_id;
    fs_mutex_t lock;
};

#define VFS_METADATA_MAX 256

struct vfs_metadata_entry {
    bool active;
    char path[MAX_PATH];
    linux_uid_t uid;
    linux_gid_t gid;
    linux_mode_t mode;
};

static struct vfs_metadata_entry vfs_metadata_table[VFS_METADATA_MAX];
static fs_mutex_t vfs_metadata_lock = FS_MUTEX_INITIALIZER;
static linux_atomic_int vfs_next_mount_ns_id = 1;

static struct vfs_mount_namespace *vfs_alloc_mount_namespace(void) {
    struct vfs_mount_namespace *mnt_ns = calloc(1, sizeof(struct vfs_mount_namespace));
    if (!mnt_ns) {
        return NULL;
    }

    atomic_init(&mnt_ns->refs, 1);
    mnt_ns->ns_id = (uint64_t)atomic_fetch_add(&vfs_next_mount_ns_id, 1);
    fs_mutex_init(&mnt_ns->lock);
    return mnt_ns;
}

static struct vfs_mount_namespace *vfs_get_mount_namespace(struct vfs_mount_namespace *mnt_ns) {
    if (mnt_ns) {
        atomic_fetch_add(&mnt_ns->refs, 1);
    }
    return mnt_ns;
}

static void vfs_put_mount_namespace(struct vfs_mount_namespace *mnt_ns) {
    if (!mnt_ns) {
        return;
    }

    if (atomic_fetch_sub(&mnt_ns->refs, 1) > 1) {
        return;
    }

    fs_mutex_destroy(&mnt_ns->lock);
    free(mnt_ns);
}

static struct vfs_mount_namespace *vfs_dup_mount_namespace(struct vfs_mount_namespace *old) {
    struct vfs_mount_namespace *new_ns;

    if (!old) {
        return vfs_alloc_mount_namespace();
    }

    new_ns = vfs_alloc_mount_namespace();
    if (!new_ns) {
        return NULL;
    }

    fs_mutex_lock(&old->lock);
    memcpy(new_ns->entries, old->entries, sizeof(new_ns->entries));
    fs_mutex_unlock(&old->lock);

    return new_ns;
}

static struct vfs_mount_namespace *vfs_task_mount_namespace(void) {
    struct task_struct *task = get_current();

    if (!task || !task->fs) {
        return NULL;
    }

    return task->fs->mnt_ns;
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

static linux_mode_t vfs_default_mode_for_virtual_path(const char *resolved_vpath, linux_mode_t current_mode) {
    if (!resolved_vpath) {
        return current_mode;
    }

    if (strcmp(resolved_vpath, "/tmp") == 0 || strcmp(resolved_vpath, "/var/tmp") == 0 ||
        strcmp(resolved_vpath, "/run") == 0) {
        return (current_mode & ~07777U) | 0777U;
    }

    return current_mode;
}

void vfs_apply_stat_metadata(const char *resolved_vpath, struct linux_stat *statbuf) {
    int idx;

    if (!statbuf) {
        return;
    }

    statbuf->st_uid = 0;
    statbuf->st_gid = 0;
    statbuf->st_mode = vfs_default_mode_for_virtual_path(resolved_vpath, statbuf->st_mode);

    fs_mutex_lock(&vfs_metadata_lock);
    idx = vfs_metadata_find_locked(resolved_vpath);
    if (idx >= 0) {
        linux_mode_t file_type = statbuf->st_mode & ~07777U;
        statbuf->st_uid = vfs_metadata_table[idx].uid;
        statbuf->st_gid = vfs_metadata_table[idx].gid;
        statbuf->st_mode = file_type | (vfs_metadata_table[idx].mode & 07777U);
    }
    fs_mutex_unlock(&vfs_metadata_lock);
}

void vfs_record_created_path(const char *resolved_vpath, linux_mode_t mode) {
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
        vfs_metadata_table[idx].uid = cred->euid;
        vfs_metadata_table[idx].gid = cred->egid;
        vfs_metadata_table[idx].mode = mode & 07777U;
    }
    fs_mutex_unlock(&vfs_metadata_lock);
}

static int vfs_record_metadata_for_stat(const char *resolved_vpath, const struct linux_stat *st) {
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
    vfs_metadata_table[idx].uid = st->st_uid;
    vfs_metadata_table[idx].gid = st->st_gid;
    vfs_metadata_table[idx].mode = st->st_mode & 07777U;
    return 0;
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
        linux_uid_t uid = vfs_metadata_table[left_idx].uid;
        linux_gid_t gid = vfs_metadata_table[left_idx].gid;
        linux_mode_t mode = vfs_metadata_table[left_idx].mode;

        vfs_metadata_table[left_idx].uid = vfs_metadata_table[right_idx].uid;
        vfs_metadata_table[left_idx].gid = vfs_metadata_table[right_idx].gid;
        vfs_metadata_table[left_idx].mode = vfs_metadata_table[right_idx].mode;
        vfs_metadata_table[right_idx].uid = uid;
        vfs_metadata_table[right_idx].gid = gid;
        vfs_metadata_table[right_idx].mode = mode;
    } else if (left_idx >= 0) {
        vfs_copy_string(right_resolved_vpath, vfs_metadata_table[left_idx].path,
                        sizeof(vfs_metadata_table[left_idx].path));
    } else if (right_idx >= 0) {
        vfs_copy_string(left_resolved_vpath, vfs_metadata_table[right_idx].path,
                        sizeof(vfs_metadata_table[right_idx].path));
    }

    fs_mutex_unlock(&vfs_metadata_lock);
}

static bool vfs_cred_has_mode_permission(const struct cred *cred, const struct linux_stat *st,
                                         linux_mode_t mask) {
    linux_mode_t perm;

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

    if (cred->euid == st->st_uid) {
        perm = (st->st_mode >> 6) & 07U;
    } else if (cred_has_group(cred, st->st_gid)) {
        perm = (st->st_mode >> 3) & 07U;
    } else {
        perm = st->st_mode & 07U;
    }

    return (perm & mask) == mask;
}

int vfs_chmod_metadata(const char *resolved_vpath, linux_mode_t mode) {
    char translated_path[MAX_PATH];
    struct linux_stat st;
    struct cred *cred = get_current_cred();
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
    if (!cred_has_cap(cred, CAP_FOWNER) && cred->euid != st.st_uid) {
        return -EPERM;
    }

    st.st_mode = (st.st_mode & ~07777U) | (mode & 07777U);

    fs_mutex_lock(&vfs_metadata_lock);
    ret = vfs_record_metadata_for_stat(resolved_vpath, &st);
    fs_mutex_unlock(&vfs_metadata_lock);
    return ret;
}

int vfs_chown_metadata(const char *resolved_vpath, linux_uid_t owner, linux_gid_t group) {
    char translated_path[MAX_PATH];
    struct linux_stat st;
    struct cred *cred = get_current_cred();
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

    if (owner != (linux_uid_t)-1) {
        st.st_uid = owner;
    }
    if (group != (linux_gid_t)-1) {
        st.st_gid = group;
    }

    fs_mutex_lock(&vfs_metadata_lock);
    ret = vfs_record_metadata_for_stat(resolved_vpath, &st);
    fs_mutex_unlock(&vfs_metadata_lock);
    return ret;
}

static int vfs_stat_virtual_backed_path(const char *resolved_vpath, const char *translated_path,
                                        struct linux_stat *st) {
    if (!resolved_vpath || !translated_path || !st) {
        return -EINVAL;
    }

    if (host_stat_impl(translated_path, st) != 0) {
        return -errno;
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
    struct linux_stat st;
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

int vfs_check_open_permission(const char *resolved_vpath, const char *translated_path, int flags) {
    struct linux_stat st;
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

static const char *vfs_relative_suffkfor_route(const struct vfs_route_entry *route,
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
    readonly = entry && ((entry->flags & MS_RDONLY) != 0);
    fs_mutex_unlock(&mnt_ns->lock);

    return readonly;
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
                                           const char *normalized_virtual_path, char *host_path,
                                           size_t host_path_len) {
    const char *backing_root;
    const char *relative_suffix;
    size_t root_len;
    size_t suffklen;
    size_t total_len;

    if (!route || !normalized_virtual_path || !host_path || host_path_len == 0) {
        return -EINVAL;
    }

    backing_root = vfs_backing_root_for_class(route->backing_class);
    if (!backing_root) {
        /* Synthetic/external: no backing, return virtual path as-is or error */
        return -ENOTSUP;
    }

    relative_suffix = vfs_relative_suffkfor_route(route, normalized_virtual_path);
    if (!relative_suffix) {
        return -EINVAL;
    }

    root_len = strlen(backing_root);
    suffklen = strlen(relative_suffix);

    if (suffklen == 0 || strcmp(relative_suffix, "/") == 0) {
        return vfs_copy_string(backing_root, host_path, host_path_len);
    }

    total_len = root_len + suffklen;
    if (total_len + 1 > host_path_len) {
        return -ENAMETOOLONG;
    }

    memcpy(host_path, backing_root, root_len);
    memcpy(host_path + root_len, relative_suffix, suffklen + 1);
    return 0;
}

static int vfs_join_host_root(const char *normalized_virtual_path, char *host_path,
                              size_t host_path_len) {
    const struct vfs_route_entry *route;

    if (vfs_ensure_backing_initialized() < 0) {
        return -ENOTSUP;
    }

    route = vfs_route_for_path(normalized_virtual_path);
    if (!route) {
        return -ENOENT;
    }

    return vfs_join_backing_root_for_route(route, normalized_virtual_path, host_path, host_path_len);
}

const char *vfs_host_backing_root(void) {
    if (vfs_ensure_backing_initialized() < 0) {
        return NULL;
    }
    return vfs_persistent_root;
}

const char *vfs_virtual_root(void) {
    return vfs_virtual_root_path;
}

struct fs_struct *alloc_fs_struct(void) {
    struct fs_struct *fs = calloc(1, sizeof(struct fs_struct));
    if (!fs)
        return NULL;

    atomic_init(&fs->users, 1);
    fs_mutex_init(&fs->lock);
    fs->umask = 022;
    fs->mnt_ns = vfs_alloc_mount_namespace();
    if (!fs->mnt_ns) {
        fs_mutex_destroy(&fs->lock);
        free(fs);
        return NULL;
    }

    return fs;
}

struct fs_struct *get_fs_struct(struct fs_struct *fs) {
    if (!fs) {
        return NULL;
    }
    atomic_fetch_add(&fs->users, 1);
    return fs;
}

void free_fs_struct(struct fs_struct *fs) {
    if (!fs)
        return;
    if (atomic_fetch_sub(&fs->users, 1) > 1)
        return;

    vfs_put_mount_namespace(fs->mnt_ns);
    fs_mutex_destroy(&fs->lock);
    free(fs);
}

struct fs_struct *dup_fs_struct(struct fs_struct *old) {
    if (!old)
        return NULL;

    struct fs_struct *new = alloc_fs_struct();
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

int fs_unshare_mount_namespace(struct fs_struct *fs) {
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

uint64_t fs_mount_namespace_id(struct fs_struct *fs) {
    uint64_t id;

    if (!fs || !fs->mnt_ns) {
        return 0;
    }

    fs_mutex_lock(&fs->lock);
    id = fs->mnt_ns ? fs->mnt_ns->ns_id : 0;
    fs_mutex_unlock(&fs->lock);
    return id;
}

/* Initialize fs_struct with virtual root path */
int fs_init_root(struct fs_struct *fs, const char *root_path) {
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
int fs_init_pwd(struct fs_struct *fs, const char *pwd_path) {
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
int fs_set_pwd(struct fs_struct *fs, const char *new_pwd) {
    return fs_init_pwd(fs, new_pwd);
}

/* Set new root - task-aware root change */
int fs_set_root(struct fs_struct *fs, const char *new_root) {
    return fs_init_root(fs, new_root);
}

/* Bootstrap Linux identity/config baseline in private rootfs */
static int vfs_bootstrap_etc_files_impl(void) {
    const char *passwd_content =
        "root:x:0:0:root:/root:/bin/sh\n"
        "ixland:x:1000:1000:IXLand User:/home/ixland:/bin/sh\n";
    const char *group_content =
        "root:x:0:\n"
        "ixland:x:1000:\n";
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
    host_ensure_directory_impl(etc_path, 0755);

    /* Create /etc/passwd */
    snprintf(file_path, sizeof(file_path), "%s/passwd", etc_path);
    fd = host_open_impl(file_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) {
        len = strlen(passwd_content);
        written = host_write_impl(fd, passwd_content, len);
        host_close_impl(fd);
        if (written != (ssize_t)len) {
            /* Non-fatal: file creation may fail in constrained environments */
        }
    }

    /* Create /etc/group */
    snprintf(file_path, sizeof(file_path), "%s/group", etc_path);
    fd = host_open_impl(file_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) {
        len = strlen(group_content);
        written = host_write_impl(fd, group_content, len);
        host_close_impl(fd);
        if (written != (ssize_t)len) {
            /* Non-fatal: file creation may fail in constrained environments */
        }
    }

    /* Create /etc/hosts */
    snprintf(file_path, sizeof(file_path), "%s/hosts", etc_path);
    fd = host_open_impl(file_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) {
        len = strlen(hosts_content);
        written = host_write_impl(fd, hosts_content, len);
        host_close_impl(fd);
        if (written != (ssize_t)len) {
            /* Non-fatal: file creation may fail in constrained environments */
        }
    }

    /* Create /etc/resolv.conf */
    snprintf(file_path, sizeof(file_path), "%s/resolv.conf", etc_path);
    fd = host_open_impl(file_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) {
        len = strlen(resolv_content);
        written = host_write_impl(fd, resolv_content, len);
        host_close_impl(fd);
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
    fs_mutex_lock(&vfs_metadata_lock);
    memset(vfs_metadata_table, 0, sizeof(vfs_metadata_table));
    fs_mutex_unlock(&vfs_metadata_lock);
}

int vfs_mount(const char *source, const char *target, const char *fstype, unsigned long flags,
              const void *data) {
    (void)data;
    char resolved_source[MAX_PATH];
    char resolved_target[MAX_PATH];
    char host_source[MAX_PATH];
    char host_target[MAX_PATH];
    struct linux_stat source_stat;
    struct linux_stat target_stat;
    int ret;
    int slot = -1;
    unsigned long supported_flags = MS_BIND | MS_REMOUNT | MS_RDONLY;
    struct vfs_mount_namespace *mnt_ns = vfs_task_mount_namespace();
    bool remount = (flags & MS_REMOUNT) != 0;

    if ((!source && !remount) || !target) {
        return -EFAULT;
    }
    if ((!remount && source[0] == '\0') || target[0] == '\0') {
        return -ENOENT;
    }
    if ((flags & ~supported_flags) != 0) {
        return -EINVAL;
    }
    if (!remount && (flags & MS_BIND) == 0) {
        return -ENOSYS;
    }
    if (remount && (flags & MS_BIND) == 0) {
        return -EINVAL;
    }
    if (!mnt_ns) {
        return -ESRCH;
    }
    if (!cred_has_cap(get_current_cred(), CAP_SYS_ADMIN)) {
        return -EPERM;
    }
    if (fstype && fstype[0] != '\0' && strcmp(fstype, "bind") != 0) {
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
                mnt_ns->entries[i].flags = (mnt_ns->entries[i].flags & ~MS_RDONLY) | (flags & MS_RDONLY) | MS_BIND;
                fs_mutex_unlock(&mnt_ns->lock);
                return 0;
            }
        }
        fs_mutex_unlock(&mnt_ns->lock);
        return -EINVAL;
    }

    ret = vfs_resolve_virtual_path_at(AT_FDCWD, source, resolved_source, sizeof(resolved_source));
    if (ret != 0) {
        return ret;
    }

    ret = vfs_translate_path(resolved_source, host_source, sizeof(host_source));
    if (ret != 0) {
        return ret;
    }
    ret = vfs_translate_path(resolved_target, host_target, sizeof(host_target));
    if (ret != 0) {
        return ret;
    }

    if (host_stat_impl(host_source, &source_stat) != 0) {
        return -errno;
    }
    if (host_stat_impl(host_target, &target_stat) != 0) {
        return -errno;
    }
    if ((S_ISDIR(source_stat.st_mode) && !S_ISDIR(target_stat.st_mode)) ||
        (!S_ISDIR(source_stat.st_mode) && S_ISDIR(target_stat.st_mode))) {
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

    mnt_ns->entries[slot].active = true;
    memcpy(mnt_ns->entries[slot].source, resolved_source, sizeof(mnt_ns->entries[slot].source));
    memcpy(mnt_ns->entries[slot].target, resolved_target, sizeof(mnt_ns->entries[slot].target));
    if (fstype && fstype[0] != '\0') {
        ret = vfs_copy_string(fstype, mnt_ns->entries[slot].fstype, sizeof(mnt_ns->entries[slot].fstype));
        if (ret != 0) {
            memset(&mnt_ns->entries[slot], 0, sizeof(mnt_ns->entries[slot]));
            fs_mutex_unlock(&mnt_ns->lock);
            return ret;
        }
    } else {
        memcpy(mnt_ns->entries[slot].fstype, "none", sizeof("none"));
    }
    mnt_ns->entries[slot].flags = flags;
    fs_mutex_unlock(&mnt_ns->lock);

    return 0;
}

int vfs_umount(const char *target) {
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
    if (!cred_has_cap(get_current_cred(), CAP_SYS_ADMIN)) {
        return -EPERM;
    }

    ret = vfs_resolve_virtual_path_at(AT_FDCWD, target, resolved_target, sizeof(resolved_target));
    if (ret != 0) {
        return ret;
    }

    fs_mutex_lock(&mnt_ns->lock);
    for (size_t i = 0; i < MAX_MOUNTS; i++) {
        if (mnt_ns->entries[i].active && strcmp(mnt_ns->entries[i].target, resolved_target) == 0) {
            memset(&mnt_ns->entries[i], 0, sizeof(mnt_ns->entries[i]));
            fs_mutex_unlock(&mnt_ns->lock);
            return 0;
        }
    }
    fs_mutex_unlock(&mnt_ns->lock);

    return -EINVAL;
}

int vfs_open(const char *path, int flags, linux_mode_t mode, int *target_fd) {
    int real_fd;

    if (!path || !target_fd) {
        return -EFAULT;
    }

    if (vfs_path_is_synthetic(path)) {
        return -ENOTSUP;
    }

    /* Current in-repo callers pass the host platform's open flags.
     * Preserve the actual call surface used by IXLandSystem while the Linux-facing
     * contract is modeled in higher layers. Validate only combinations we can
     * represent coherently now.
     */
    if ((flags & O_EXCL) && !(flags & O_CREAT)) {
        return -EINVAL;
    }

    real_fd = host_open_impl(path, flags, mode);
    if (real_fd < 0) {
        return -errno;
    }

    *target_fd = real_fd;
    return 0;
}

int vfs_close(struct file *file) {
    (void)file;
    return -ENOSYS;
}

int vfs_lookup(const char *path, struct dentry **dentry) {
    (void)path;
    (void)dentry;
    return -ENOSYS;
}

int vfs_path_walk(const char *path, struct dentry **dentry) {
    (void)path;
    (void)dentry;
    return -ENOSYS;
}

int vfs_mkdir(const char *path, linux_mode_t mode) {
    (void)path;
    (void)mode;
    return -ENOSYS;
}

int vfs_unlink(const char *path) {
    (void)path;
    return -ENOSYS;
}

int vfs_rmdir(const char *path) {
    (void)path;
    return -ENOSYS;
}

static int vfs_join_virtual_path(const char *base_path, const char *suffix, char *joined_path,
                                 size_t joined_path_len) {
    size_t base_len;
    size_t suffklen;
    size_t suffkoffset;

    if (!base_path || !suffix || !joined_path || joined_path_len == 0) {
        return -EINVAL;
    }

    base_len = strlen(base_path);
    suffklen = strlen(suffix);
    suffkoffset = (suffix[0] == '/') ? 1 : 0;

    if (base_len == 0) {
        return -EINVAL;
    }

    if (strcmp(base_path, "/") == 0) {
        if (suffklen - suffkoffset + 1 >= joined_path_len) {
            return -ENAMETOOLONG;
        }
        joined_path[0] = '/';
        memcpy(joined_path + 1, suffix + suffkoffset, suffklen - suffkoffset + 1);
        return 0;
    }

    if (base_len + 1 + suffklen - suffkoffset >= joined_path_len) {
        return -ENAMETOOLONG;
    }

    memcpy(joined_path, base_path, base_len);
    joined_path[base_len] = '/';
    memcpy(joined_path + base_len + 1, suffix + suffkoffset, suffklen - suffkoffset + 1);
    return 0;
}

int vfs_resolve_virtual_path_task(const char *vpath, char *resolved_vpath, size_t resolved_vpath_len,
                                  struct fs_struct *fs) {
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

int vfs_getcwd_path_task(struct fs_struct *fs, char *vpath, size_t vpath_len) {
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
    struct task_struct *task;
    struct fs_struct *fs = NULL;
    char dir_virtual_path[MAX_PATH];
    char joined_virtual_path[MAX_PATH];
    void *entry;
    int ret;

    if (!vpath || !resolved_vpath || resolved_vpath_len == 0) {
        return -EINVAL;
    }

    if (vpath[0] == '/' || dirfd == AT_FDCWD) {
        task = get_current();
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
        return -errno;
    }

    ret = vfs_join_virtual_path(dir_virtual_path, vpath, joined_virtual_path, sizeof(joined_virtual_path));
    if (ret != 0) {
        return ret;
    }

    return vfs_normalize_linux_path(joined_virtual_path, resolved_vpath, resolved_vpath_len);
}

int vfs_translate_path_task(const char *vpath, char *host_path, size_t host_path_len,
                            struct fs_struct *fs) {
    char resolved_virtual[MAX_PATH];
    char mounted_virtual[MAX_PATH];
    int ret;

    if (!vpath || !host_path || host_path_len == 0) {
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

    return vfs_join_host_root(mounted_virtual, host_path, host_path_len);
}

int vfs_translate_path_at(int dirfd, const char *vpath, char *host_path, size_t host_path_len) {
    char resolved_virtual[MAX_PATH];
    char mounted_virtual[MAX_PATH];
    int ret;

    if (!vpath || !host_path || host_path_len == 0) {
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

    return vfs_join_host_root(mounted_virtual, host_path, host_path_len);
}

/* Legacy API: translate path using hardcoded root (for backward compatibility) */
int vfs_translate_path(const char *vpath, char *host_path, size_t host_path_len) {
    return vfs_translate_path_task(vpath, host_path, host_path_len, NULL);
}

/* Backing initialization - must be called before path translation */
static int vfs_ensure_backing_initialized(void) {
    if (vfs_backing_initialized) {
        return 0;
    }

    int ret;

    /* Discover persistent (Application Support) root */
    ret = vfs_discover_persistent_root(vfs_persistent_root, sizeof(vfs_persistent_root));
    if (ret < 0) {
        return -ENOTSUP;
    }

    /* Discover cache root */
    ret = vfs_discover_cache_root(vfs_cache_root, sizeof(vfs_cache_root));
    if (ret < 0) {
        /* Fall back to persistent if caches not available */
        strncpy(vfs_cache_root, vfs_persistent_root, sizeof(vfs_cache_root) - 1);
        vfs_cache_root[sizeof(vfs_cache_root) - 1] = '\0';
    }

    /* Discover temp root */
    ret = vfs_discover_temp_root(vfs_temp_root, sizeof(vfs_temp_root));
    if (ret < 0) {
        /* Fall back to temporary subdirectory of persistent */
        snprintf(vfs_temp_root, sizeof(vfs_temp_root), "%s/.ixland.tmp", vfs_persistent_root);
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

int vfs_reverse_translate(const char *host_path, char *vpath, size_t vpath_len) {
    const struct vfs_route_entry *best_route = NULL;
    const char *best_host_suffix = NULL;
    size_t best_prefklen = 0;
    size_t i;
    int ret;

    if (!host_path || !vpath || vpath_len == 0) {
        return -EINVAL;
    }

    ret = vfs_ensure_backing_initialized();
    if (ret < 0) {
        return ret;
    }

    for (i = 0; i < vfs_route_table_count; i++) {
        const struct vfs_route_entry *route = &vfs_route_table[i];
        const char *backing_root;
        const char *host_suffix;
        size_t root_len;
        size_t prefklen;
        char route_host_prefix[MAX_PATH];

        if (!route->reverse_linux_prefix) {
            continue;
        }

        backing_root = vfs_backing_root_for_class(route->backing_class);
        if (!backing_root) {
            continue;
        }

        root_len = strlen(backing_root);
        if (route->strip_linux_prefix || strcmp(route->reverse_linux_prefix, "/") == 0) {
            if (vfs_copy_string(backing_root, route_host_prefix, sizeof(route_host_prefix)) != 0) {
                continue;
            }
        } else {
            ret = snprintf(route_host_prefix, sizeof(route_host_prefix), "%s%s", backing_root,
                           route->reverse_linux_prefix);
            if (ret < 0 || (size_t)ret >= sizeof(route_host_prefix)) {
                continue;
            }
        }

        prefklen = strlen(route_host_prefix);
        if (strncmp(host_path, route_host_prefix, prefklen) != 0) {
            continue;
        }

        host_suffix = host_path + prefklen;
        if (*host_suffix != '\0' && *host_suffix != '/') {
            continue;
        }

        if (prefklen > best_prefklen) {
            best_route = route;
            best_host_suffix = host_suffix;
            best_prefklen = prefklen;
        }
    }

    if (!best_route || !best_host_suffix) {
        return -EXDEV;
    }

    if (strcmp(best_route->reverse_linux_prefix, "/") == 0) {
        if (*best_host_suffix == '\0') {
            return vfs_copy_string(vfs_virtual_root_path, vpath, vpath_len);
        }
        return vfs_normalize_linux_path(best_host_suffix, vpath, vpath_len);
    }

    if (*best_host_suffix == '\0') {
        return vfs_copy_string(best_route->reverse_linux_prefix, vpath, vpath_len);
    }

    {
        char work_buf[MAX_PATH];
        ret = snprintf(work_buf, sizeof(work_buf), "%s%s", best_route->reverse_linux_prefix,
                       best_host_suffix);
        if (ret < 0 || (size_t)ret >= sizeof(work_buf)) {
            return -ENAMETOOLONG;
        }
        return vfs_normalize_linux_path(work_buf, vpath, vpath_len);
    }
}

int vfs_stat_path(const char *pathname, struct linux_stat *statbuf) {
    if (!pathname || !statbuf) {
        return -EFAULT;
    }
    if (vfs_path_is_synthetic(pathname)) {
        return -ENOENT;
    }
    return host_stat_impl(pathname, statbuf);
}

int vfs_lstat(const char *pathname, struct linux_stat *statbuf) {
    if (!pathname || !statbuf) {
        return -EFAULT;
    }
    if (vfs_path_is_synthetic(pathname)) {
        return -ENOENT;
    }
    return host_lstat_impl(pathname, statbuf);
}

static const char *vfs_proc_current_task_suffix(const char *vpath, char *self_path, size_t self_path_len) {
    struct task_struct *task;
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

    task = get_current();
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

proc_self_path_class_t vfs_classify_proc_self_path(const char *vpath) {
    char mapped[MAX_PATH];
    const char *suffix;

    if (!vpath) {
        return PROC_SELF_NONE;
    }

    suffix = vfs_proc_current_task_suffix(vpath, mapped, sizeof(mapped));
    if (!suffix) {
        return PROC_SELF_NONE;
    }

    if (strcmp(suffix, "") == 0) {
        return PROC_SELF_DIR;
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
    if (strcmp(suffix, "/comm") == 0) {
        return PROC_SELF_COMM_FILE;
    }
    if (strcmp(suffix, "/stat") == 0) {
        return PROC_SELF_STAT_FILE;
    }
    if (strcmp(suffix, "/statm") == 0) {
        return PROC_SELF_STATM_FILE;
    }
    if (strcmp(suffix, "/status") == 0) {
        return PROC_SELF_STATUS_FILE;
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
        (strcmp(suffix, "/ns/pid") == 0)) {
        return PROC_SELF_NS_LINK;
    }
    return PROC_SELF_NONE;
}

int vfs_proc_self_fd_link_target(const char *vpath, char *target, size_t target_len) {
    const char *fd_str;
    char *endptr;
    long fd_num;
    void *entry;
    int ret;

    if (!vpath || !target || target_len == 0) {
        return -EINVAL;
    }

    if (strncmp(vpath, "/proc/self/fd/", 14) != 0) {
        return -EINVAL;
    }

    fd_str = vpath + 14;
    fd_num = strtol(fd_str, &endptr, 10);
    if (*endptr != '\0' || fd_num < 0 || fd_num >= NR_OPEN_DEFAULT) {
        return -ENOENT;
    }

    if (!fdtable_is_used_impl((int)fd_num)) {
        return -ENOENT;
    }

    entry = get_fd_entry_impl((int)fd_num);
    if (!entry) {
        return -ENOENT;
    }

    ret = get_fd_path_impl(entry, target, target_len);
    put_fd_entry_impl(entry);

    if (ret != 0) {
        return -ENOENT;
    }

    return 0;
}

int vfs_proc_self_cwd_target(char *target, size_t target_len) {
    struct task_struct *task;

    if (!target || target_len == 0) {
        return -EINVAL;
    }

    task = get_current();
    if (!task || !task->fs) {
        return -ESRCH;
    }

    return vfs_getcwd_path_task(task->fs, target, target_len);
}

int vfs_proc_self_exe_target(char *target, size_t target_len) {
    struct task_struct *task;
    size_t exe_len;

    if (!target || target_len == 0) {
        return -EINVAL;
    }

    task = get_current();
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
    struct task_struct *task;
    const char *name;
    uint64_t id;
    int ret;

    if (!vpath || !target || target_len == 0) {
        return -EINVAL;
    }

    task = get_current();
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

int vfs_proc_self_cmdline_content(char *buf, size_t buf_len) {
    struct task_struct *task;
    size_t pos = 0;
    int i;

    if (!buf || buf_len == 0) {
        return -EINVAL;
    }

    task = get_current();
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

int vfs_proc_self_comm_content(char *buf, size_t buf_len) {
    struct task_struct *task;
    size_t comm_len;

    if (!buf || buf_len == 0) {
        return -EINVAL;
    }

    task = get_current();
    if (!task) {
        return -ESRCH;
    }

    comm_len = strnlen(task->comm, TASK_COMM_LEN);
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

int vfs_proc_self_stat_content(char *buf, size_t buf_len) {
    struct task_struct *task;
    int ret;
    char state_char;

    if (!buf || buf_len == 0) {
        return -EINVAL;
    }

    task = get_current();
    if (!task) {
        return -ESRCH;
    }

    switch (atomic_load(&task->state)) {
        case TASK_RUNNING:
            state_char = 'R';
            break;
        case TASK_INTERRUPTIBLE:
            state_char = 'S';
            break;
        case TASK_UNINTERRUPTIBLE:
            state_char = 'D';
            break;
        case TASK_STOPPED:
            state_char = 'T';
            break;
        case TASK_ZOMBIE:
            state_char = 'Z';
            break;
        default:
            state_char = 'R';
            break;
    }

    ret = snprintf(buf, buf_len,
        "%d (%s) %c %d %d %d 0 -1 0 0 0 0 0 0 0 0 0 0 1 0 %llu 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0\n",
        task->pid,
        task->comm,
        state_char,
        task->ppid,
        task->pgid,
        task->sid,
        (unsigned long long)(task->start_time_ns / 1000000000ULL)
    );

    if (ret < 0) {
        return -EINVAL;
    }
    if ((size_t)ret >= buf_len) {
        return (int)(buf_len - 1);
    }
    return ret;
}

int vfs_proc_self_statm_content(char *buf, size_t buf_len) {
    int ret;

    if (!buf || buf_len == 0) {
        return -EINVAL;
    }

    ret = snprintf(buf, buf_len, "0 0 0 0 0 0 0\n");

    if (ret < 0) {
        return -EINVAL;
    }
    if ((size_t)ret >= buf_len) {
        return (int)(buf_len - 1);
    }
    return ret;
}

int vfs_proc_self_fdinfo_content(int fd_num, char *buf, size_t buf_len) {
    void *entry;
    off_t offset;
    int flags;
    int fd_flags;
    int ret;

    if (!buf || buf_len == 0) {
        return -EINVAL;
    }

    if (fd_num < 0 || fd_num >= NR_OPEN_DEFAULT) {
        return -ENOENT;
    }

    entry = get_fd_entry_impl(fd_num);
    if (!entry) {
        return -ENOENT;
    }

    offset = get_fd_offset_impl(entry);
    flags = get_fd_flags_impl(entry);
    fd_flags = get_fd_descriptor_flags_impl(entry);
    put_fd_entry_impl(entry);

    if (fd_flags & FD_CLOEXEC) {
        flags |= O_CLOEXEC;
    }

    ret = snprintf(buf, buf_len, "pos:\t%lld\nflags:\t0%o\n", (long long)offset, flags);

    if (ret < 0) {
        return -EINVAL;
    }
    if ((size_t)ret >= buf_len) {
        return (int)(buf_len - 1);
    }
    return ret;
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

int vfs_proc_self_mountinfo_content(char *buf, size_t buf_len) {
    struct vfs_mount_namespace *mnt_ns;
    size_t pos = 0;
    int mount_id = 2;

    if (!buf || buf_len == 0) {
        return -EINVAL;
    }

    mnt_ns = vfs_task_mount_namespace();
    if (!mnt_ns) {
        return -ESRCH;
    }

    if (vfs_proc_append(buf, buf_len, &pos, "1 0 0:1 / / rw - ixland-root ixland-root rw\n") != 0) {
        return (int)pos;
    }

    fs_mutex_lock(&mnt_ns->lock);
    for (size_t i = 0; i < MAX_MOUNTS; i++) {
        struct vfs_mount_entry *entry = &mnt_ns->entries[i];

        if (!entry->active) {
            continue;
        }

        const char *mode = (entry->flags & MS_RDONLY) ? "ro" : "rw";
        const char *fstype = entry->fstype[0] ? entry->fstype : "none";
        if (vfs_proc_append(buf, buf_len, &pos, "%d 1 0:%d %s %s %s - %s %s %s,bind\n",
                            mount_id, mount_id, entry->source, entry->target, mode,
                            fstype, entry->source, mode) != 0) {
            break;
        }
        mount_id++;
    }
    fs_mutex_unlock(&mnt_ns->lock);

    return (int)pos;
}

int vfs_proc_self_mounts_content(char *buf, size_t buf_len) {
    struct vfs_mount_namespace *mnt_ns;
    size_t pos = 0;

    if (!buf || buf_len == 0) {
        return -EINVAL;
    }

    mnt_ns = vfs_task_mount_namespace();
    if (!mnt_ns) {
        return -ESRCH;
    }

    if (vfs_proc_append(buf, buf_len, &pos, "ixland-root / ixland-root rw 0 0\n") != 0) {
        return (int)pos;
    }

    fs_mutex_lock(&mnt_ns->lock);
    for (size_t i = 0; i < MAX_MOUNTS; i++) {
        struct vfs_mount_entry *entry = &mnt_ns->entries[i];

        if (!entry->active) {
            continue;
        }

        const char *mode = (entry->flags & MS_RDONLY) ? "ro" : "rw";
        const char *fstype = entry->fstype[0] ? entry->fstype : "none";
        if (vfs_proc_append(buf, buf_len, &pos, "%s %s %s %s,bind 0 0\n",
                            entry->source, entry->target, fstype, mode) != 0) {
            break;
        }
    }
    fs_mutex_unlock(&mnt_ns->lock);

    return (int)pos;
}

int vfs_proc_self_status_content(char *buf, size_t buf_len) {
    struct task_struct *task;
    struct cred *cred;
    int ret;
    char state_char;

    if (!buf || buf_len == 0) {
        return -EINVAL;
    }

    task = get_current();
    if (!task) {
        return -ESRCH;
    }

    cred = get_current_cred();
    if (!cred) {
        return -ESRCH;
    }

    switch (atomic_load(&task->state)) {
        case TASK_RUNNING:
            state_char = 'R';
            break;
        case TASK_INTERRUPTIBLE:
            state_char = 'S';
            break;
        case TASK_UNINTERRUPTIBLE:
            state_char = 'D';
            break;
        case TASK_STOPPED:
            state_char = 'T';
            break;
        case TASK_ZOMBIE:
            state_char = 'Z';
            break;
        default:
            state_char = 'R';
            break;
    }

    ret = snprintf(buf, buf_len,
        "Name:\t%s\n"
        "State:\t%c (running)\n"
        "Tgid:\t%d\n"
        "Pid:\t%d\n"
        "PPid:\t%d\n"
        "TracerPid:\t0\n"
        "Uid:\t%u\t%u\t%u\t%u\n"
        "Gid:\t%u\t%u\t%u\t%u\n"
        "NStgid:\t%d\n"
        "NSpid:\t%d\n"
        "NSpgid:\t%d\n"
        "NSsid:\t%d\n",
        task->comm,
        state_char,
        task->tgid,
        task->pid,
        task->ppid,
        cred->uid, cred->euid, cred->suid, cred->suid,
        cred->gid, cred->egid, cred->sgid, cred->sgid,
        task->ns_pid,
        task->ns_pid,
        task->pgid,
        task->sid
    );

    if (ret < 0) {
        return -EINVAL;
    }
    if ((size_t)ret >= buf_len) {
        return (int)(buf_len - 1);
    }
    return ret;
}

int vfs_access(const char *pathname, int mode) {
    if (!pathname) {
        return -EFAULT;
    }
    if (vfs_path_is_synthetic_root(pathname)) {
        return 0;
    }
    if (vfs_path_is_synthetic_dev_node(pathname) != SYNTHETIC_DEV_NONE) {
        return 0;
    }
    if (strcmp(pathname, "/dev/tty") == 0 || vfs_path_is_synthetic_dev_dir(pathname)) {
        return 0;
    }
    if (pty_is_virtual_slave_path_impl(pathname)) {
        return 0;
    }
    if (vfs_classify_proc_self_path(pathname) != PROC_SELF_NONE) {
        return 0;
    }
    if (vfs_path_is_synthetic(pathname)) {
        return -ENOENT;
    }
    return host_access_impl(pathname, mode);
}

int vfs_fstatat(int dirfd, const char *pathname, struct linux_stat *statbuf, int flags) {
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

    ret = vfs_resolve_virtual_path_at(dirfd, pathname, resolved_virtual, sizeof(resolved_virtual));
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
        if (proc_class == PROC_SELF_DIR || proc_class == PROC_SELF_FD_DIR || proc_class == PROC_SELF_FDINFO_DIR ||
            proc_class == PROC_SELF_NS_DIR) {
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
            ret = vfs_proc_self_cwd_target(link_target, sizeof(link_target));
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
            ret = vfs_proc_self_exe_target(link_target, sizeof(link_target));
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
        } else if (proc_class == PROC_SELF_CMDLINE_FILE || proc_class == PROC_SELF_COMM_FILE ||
                   proc_class == PROC_SELF_STAT_FILE || proc_class == PROC_SELF_STATM_FILE ||
                   proc_class == PROC_SELF_STATUS_FILE || proc_class == PROC_SELF_MOUNTINFO_FILE ||
                   proc_class == PROC_SELF_MOUNTS_FILE) {
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
            const char *fd_str = resolved_virtual + 18;
            char *endptr;
            long fd_num = strtol(fd_str, &endptr, 10);
            if (*endptr != '\0' || fd_num < 0 || fd_num >= NR_OPEN_DEFAULT) {
                return -ENOENT;
            }
            if (!fdtable_is_used_impl((int)fd_num)) {
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
    ret = vfs_join_host_root(resolved_virtual, translated_path, sizeof(translated_path));
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

    if (flags & AT_EACCESS) {
        return -ENOTSUP;
    }

    ret = vfs_resolve_virtual_path_at(dirfd, pathname, resolved_virtual, sizeof(resolved_virtual));
    if (ret != 0) {
        return ret;
    }

    if (vfs_path_is_synthetic_root(resolved_virtual)) {
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

    ret = vfs_translate_path_at(dirfd, pathname, translated_path, sizeof(translated_path));
    if (ret != 0) {
        return ret;
    }

    if (flags & AT_SYMLINK_NOFOLLOW) {
        return -ENOTSUP;
    }

    {
        struct linux_stat st;
        struct cred *cred = get_current_cred();

        ret = vfs_stat_virtual_backed_path(resolved_virtual, translated_path, &st);
        if (ret != 0) {
            return ret;
        }

        if ((mode & 4) != 0 && !vfs_cred_has_mode_permission(cred, &st, 04U)) {
            return -EACCES;
        }
        if ((mode & 2) != 0 && !vfs_cred_has_mode_permission(cred, &st, 02U)) {
            return -EACCES;
        }
        if ((mode & 1) != 0 && !vfs_cred_has_mode_permission(cred, &st, 01U)) {
            return -EACCES;
        }
    }

    return 0;
}

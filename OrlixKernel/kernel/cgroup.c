/* OrlixKernel/kernel/cgroup.c
 * Virtual cgroup hierarchy and membership ownership.
 */

#include "cgroup.h"

#include "cred.h"
#include "task.h"
#include "../private/kernel/task_state.h"

#include <linux/errno.h>
#include <linux/atomic.h>
#include <linux/gfp_types.h>
#include <linux/stdarg.h>
#include <linux/sprintf.h>
#include <linux/string.h>

#include <linux/capability.h>

extern void *__kmalloc_noprof(size_t size, gfp_t flags);
extern void kfree(const void *objp);

struct cgroup {
    char path[MAX_PATH];
    atomic_t refs;
    unsigned int members;
    int pids_max;
    bool freezer_frozen;
    bool subtree_pids;
    bool subtree_freezer;
    kernel_mutex_t lock;
    struct cgroup *parent;
    struct cgroup *children;
    struct cgroup *next_sibling;
};

static struct cgroup *root_cgroup;
static kernel_mutex_t cgroup_lock = KERNEL_MUTEX_INITIALIZER;
static atomic64_t next_cgroup_ns_id = ATOMIC64_INIT(2);

static bool cgroupfs_current_can_control(void) {
    struct task *task = task_current();
    uint64_t owner_user_ns_id;

    if (!task) {
        return false;
    }
    owner_user_ns_id = task->cgroup_ns_owner_user_ns_id ? task->cgroup_ns_owner_user_ns_id : 1;
    return cred_has_cap_in_user_namespace(get_current_cred(), owner_user_ns_id, CAP_SYS_ADMIN);
}

static void cgroup_free_tree(struct cgroup *cgrp) {
    if (!cgrp) {
        return;
    }
    while (cgrp->children) {
        struct cgroup *child = cgrp->children;
        cgrp->children = child->next_sibling;
        cgroup_free_tree(child);
    }
    kernel_mutex_destroy(&cgrp->lock);
    kfree(cgrp);
}

static bool cgroup_path_is_prefix(const char *parent, const char *child) {
    size_t parent_len;

    if (!parent || !child) {
        return false;
    }
    if (strcmp(parent, "/") == 0) {
        return child[0] == '/';
    }
    parent_len = strlen(parent);
    return strncmp(parent, child, parent_len) == 0 &&
           (child[parent_len] == '\0' || child[parent_len] == '/');
}

static const char *cgroup_name(const struct cgroup *cgrp) {
    const char *slash;

    if (!cgrp || strcmp(cgrp->path, "/") == 0) {
        return "";
    }
    slash = strrchr(cgrp->path, '/');
    return slash ? slash + 1 : cgrp->path;
}

static struct cgroup *cgroup_find_absolute_locked(const char *path) {
    struct cgroup *stack[128];
    size_t sp = 0;

    if (!path || !root_cgroup || path[0] != '/') {
        return NULL;
    }
    stack[sp++] = root_cgroup;
    while (sp > 0) {
        struct cgroup *cur = stack[--sp];
        if (strcmp(cur->path, path) == 0) {
            return cur;
        }
        for (struct cgroup *child = cur->children; child; child = child->next_sibling) {
            if (sp < sizeof(stack) / sizeof(stack[0])) {
                stack[sp++] = child;
            }
        }
    }
    return NULL;
}

static int cgroup_absolute_from_visible(const struct task *task, const char *visible,
                                        char *out, size_t out_len) {
    const char *root_path;
    int ret;

    if (!task || !visible || !out || out_len == 0 || visible[0] != '/') {
        return -EINVAL;
    }
    root_path = task->cgroup_ns_root ? task->cgroup_ns_root->path : "/";
    if (strcmp(root_path, "/") == 0) {
        if (strlen(visible) >= out_len) {
            return -ENAMETOOLONG;
        }
        memcpy(out, visible, strlen(visible) + 1);
        return 0;
    }
    if (strcmp(visible, "/") == 0) {
        if (strlen(root_path) >= out_len) {
            return -ENAMETOOLONG;
        }
        memcpy(out, root_path, strlen(root_path) + 1);
        return 0;
    }
    ret = scnprintf(out, out_len, "%s%s", root_path, visible);
    if (ret < 0) {
        return -EINVAL;
    }
    if ((size_t)ret >= out_len - (out_len > 0 ? 1 : 0)) {
        return -ENAMETOOLONG;
    }
    return 0;
}

static int cgroup_visible_path_for_task(const struct task *task, char *out, size_t out_len) {
    const char *root_path;
    const char *task_path;
    size_t root_len;

    if (!task || !out || out_len == 0) {
        return -EINVAL;
    }
    task_path = task->cgroup ? task->cgroup->path : "/";
    root_path = task->cgroup_ns_root ? task->cgroup_ns_root->path : "/";
    if (!cgroup_path_is_prefix(root_path, task_path)) {
        root_path = "/";
    }
    if (strcmp(root_path, "/") == 0) {
        if (strlen(task_path) >= out_len) {
            return -ENAMETOOLONG;
        }
        memcpy(out, task_path, strlen(task_path) + 1);
        return 0;
    }
    root_len = strlen(root_path);
    if (task_path[root_len] == '\0') {
        if (out_len < 2) {
            return -ENAMETOOLONG;
        }
        memcpy(out, "/", 2);
        return 0;
    }
    if (strlen(task_path + root_len) >= out_len) {
        return -ENAMETOOLONG;
    }
    memcpy(out, task_path + root_len, strlen(task_path + root_len) + 1);
    return 0;
}

static bool cgroup_task_is_visible_to_task(const struct task *viewer,
                                           const struct task *target) {
    const char *root_path;
    const char *target_path;

    if (!viewer || !target) {
        return false;
    }
    root_path = viewer->cgroup_ns_root ? viewer->cgroup_ns_root->path : "/";
    target_path = target->cgroup ? target->cgroup->path : "/";
    return cgroup_path_is_prefix(root_path, target_path);
}

int cgroup_init(void) {
    kernel_mutex_lock(&cgroup_lock);
    if (root_cgroup) {
        kernel_mutex_unlock(&cgroup_lock);
        return 0;
    }

    root_cgroup = __kmalloc_noprof(sizeof(*root_cgroup), GFP_KERNEL | __GFP_ZERO);
    if (!root_cgroup) {
        kernel_mutex_unlock(&cgroup_lock);
        return -ENOMEM;
    }

    memcpy(root_cgroup->path, "/", 2);
    atomic_set(&root_cgroup->refs, 1);
    root_cgroup->pids_max = -1;
    kernel_mutex_init(&root_cgroup->lock);
    kernel_mutex_unlock(&cgroup_lock);
    return 0;
}

void cgroup_deinit(void) {
    struct cgroup *root;

    kernel_mutex_lock(&cgroup_lock);
    root = root_cgroup;
    root_cgroup = NULL;
    kernel_mutex_unlock(&cgroup_lock);

    if (!root) {
        return;
    }
    cgroup_free_tree(root);
}

struct cgroup *cgroup_get(struct cgroup *cgrp) {
    if (!cgrp) {
        return NULL;
    }
    atomic_inc(&cgrp->refs);
    return cgrp;
}

void cgroup_put(struct cgroup *cgrp) {
    if (!cgrp) {
        return;
    }
    if (atomic_dec_return(&cgrp->refs) != 0) {
        return;
    }
    cgroup_free_tree(cgrp);
}

struct cgroup *cgroup_root(void) {
    if (!root_cgroup && cgroup_init() != 0) {
        return NULL;
    }
    return cgroup_get(root_cgroup);
}

int task_attach_cgroup(struct task *task, struct cgroup *cgrp) {
    struct cgroup *old;
    int blocked = 0;

    if (!task || !cgrp) {
        return -EINVAL;
    }

    old = task->cgroup;
    if (old == cgrp) {
        return 0;
    }

    kernel_mutex_lock(&cgrp->lock);
    if (cgrp->freezer_frozen) {
        blocked = EBUSY;
    } else if (cgrp->pids_max >= 0 && cgrp->members >= (unsigned int)cgrp->pids_max) {
        blocked = EAGAIN;
    }
    if (blocked != 0) {
        kernel_mutex_unlock(&cgrp->lock);
        return -blocked;
    }
    cgrp->members++;
    kernel_mutex_unlock(&cgrp->lock);

    cgroup_get(cgrp);

    task->cgroup = cgrp;

    if (old) {
        kernel_mutex_lock(&old->lock);
        if (old->members > 0) {
            old->members--;
        }
        kernel_mutex_unlock(&old->lock);
        cgroup_put(old);
    }

    return 0;
}

int cgroup_attach_task_path(struct task *task, const char *path) {
    struct cgroup *target;
    int ret;

    if (!task || !path) {
        return -EINVAL;
    }
    if (cgroup_init() != 0) {
        return -ENOMEM;
    }
    kernel_mutex_lock(&cgroup_lock);
    target = cgroup_find_absolute_locked(path);
    if (target) {
        cgroup_get(target);
    }
    kernel_mutex_unlock(&cgroup_lock);
    if (!target) {
        return -ENOENT;
    }
    ret = task_attach_cgroup(task, target);
    cgroup_put(target);
    return ret;
}

void task_detach_cgroup(struct task *task) {
    struct cgroup *old;

    if (!task || !task->cgroup) {
        return;
    }

    old = task->cgroup;
    task->cgroup = NULL;
    kernel_mutex_lock(&old->lock);
    if (old->members > 0) {
        old->members--;
    }
    kernel_mutex_unlock(&old->lock);
    cgroup_put(old);
}

int task_unshare_cgroup_namespace(struct task *task) {
    struct cgroup *old_root;

    if (!task || !task->cgroup) {
        return -EINVAL;
    }
    old_root = task->cgroup_ns_root;
    task->cgroup_ns_root = cgroup_get(task->cgroup);
    task->cgroup_ns_id = (uint64_t)atomic64_inc_return(&next_cgroup_ns_id) - 1ULL;
    task->cgroup_ns_owner_user_ns_id = cred_user_namespace_id(task->cred);
    if (task->cgroup_ns_owner_user_ns_id == 0) {
        task->cgroup_ns_owner_user_ns_id = 1;
    }
    cgroup_put(old_root);
    return 0;
}

int task_reset_cgroup_namespace(struct task *task) {
    struct cgroup *old_root;
    struct cgroup *root;

    if (!task) {
        return -EINVAL;
    }
    root = cgroup_root();
    if (!root) {
        return -ENOMEM;
    }
    old_root = task->cgroup_ns_root;
    task->cgroup_ns_root = root;
    task->cgroup_ns_id = 1;
    task->cgroup_ns_owner_user_ns_id = 1;
    cgroup_put(old_root);
    return 0;
}

uint64_t task_cgroup_namespace_id(const struct task *task) {
    if (!task) {
        return 0;
    }
    return task->cgroup_ns_id == 0 ? 1 : task->cgroup_ns_id;
}

uint64_t task_cgroup_namespace_owner_user_ns_id(const struct task *task) {
    if (!task || task->cgroup_ns_owner_user_ns_id == 0) {
        return 1;
    }
    return task->cgroup_ns_owner_user_ns_id;
}

const char *task_cgroup_path(const struct task *task) {
    if (!task || !task->cgroup) {
        return "/";
    }
    return task->cgroup->path;
}

unsigned int task_cgroup_member_count(const struct task *task) {
    struct cgroup *cgrp;
    unsigned int count;

    if (!task || !task->cgroup) {
        return 0;
    }

    cgrp = task->cgroup;
    kernel_mutex_lock(&cgrp->lock);
    count = cgrp->members;
    kernel_mutex_unlock(&cgrp->lock);
    return count;
}

static int task_cgroup_proc_content_for_viewer(const struct task *viewer,
                                               const struct task *task,
                                               char *buf, size_t buflen) {
    int ret;
    char visible[MAX_PATH];
    const char *root_path;
    const char *task_path;
    size_t root_len;

    if (!buf || buflen == 0) {
        return -EINVAL;
    }
    if (!viewer || !task) {
        return -ESRCH;
    }

    task_path = task->cgroup ? task->cgroup->path : "/";
    root_path = viewer->cgroup_ns_root ? viewer->cgroup_ns_root->path : "/";
    if (!cgroup_path_is_prefix(root_path, task_path)) {
        return -EACCES;
    }
    if (strcmp(root_path, "/") == 0) {
        if (strlen(task_path) >= sizeof(visible)) {
            return -ENAMETOOLONG;
        }
        memcpy(visible, task_path, strlen(task_path) + 1);
    } else {
        root_len = strlen(root_path);
        if (task_path[root_len] == '\0') {
            memcpy(visible, "/", 2);
        } else {
            if (strlen(task_path + root_len) >= sizeof(visible)) {
                return -ENAMETOOLONG;
            }
            memcpy(visible, task_path + root_len, strlen(task_path + root_len) + 1);
        }
    }
    ret = scnprintf(buf, buflen, "0::%s\n", visible);
    if (ret < 0) {
        return -EINVAL;
    }
    if (buflen > 0 && (size_t)ret >= buflen - 1) {
        return (int)(buflen - 1);
    }
    return ret;
}

int task_cgroup_proc_content(const struct task *task, char *buf, size_t buflen) {
    int ret;
    char visible[MAX_PATH];

    if (!buf || buflen == 0) {
        return -EINVAL;
    }
    if (!task) {
        return -ESRCH;
    }
    ret = cgroup_visible_path_for_task(task, visible, sizeof(visible));
    if (ret != 0) {
        return ret;
    }
    ret = scnprintf(buf, buflen, "0::%s\n", visible);
    if (ret < 0) {
        return -EINVAL;
    }
    if (buflen > 0 && (size_t)ret >= buflen - 1) {
        return (int)(buflen - 1);
    }
    return ret;
}

static int cgroupfs_visible_path(const char *path, char *visible, size_t visible_len,
                                 const char **file_name) {
    const char *prefix = "/sys/fs/cgroup";
    size_t prefix_len = strlen(prefix);
    const char *suffix;
    char tmp[MAX_PATH];
    char *last_slash;

    if (file_name) {
        *file_name = NULL;
    }
    if (!path || !visible || visible_len == 0 ||
        strncmp(path, prefix, prefix_len) != 0 ||
        (path[prefix_len] != '\0' && path[prefix_len] != '/')) {
        return -EINVAL;
    }
    suffix = path + prefix_len;
    if (suffix[0] == '\0') {
        memcpy(visible, "/", 2);
        return 0;
    }
    if (strlen(suffix) >= sizeof(tmp)) {
        return -ENAMETOOLONG;
    }
    memcpy(tmp, suffix, strlen(suffix) + 1);
    last_slash = strrchr(tmp, '/');
    if (last_slash && file_name &&
        (strcmp(last_slash + 1, "cgroup.procs") == 0 ||
         strcmp(last_slash + 1, "cgroup.controllers") == 0 ||
         strcmp(last_slash + 1, "cgroup.subtree_control") == 0 ||
         strcmp(last_slash + 1, "pids.max") == 0 ||
         strcmp(last_slash + 1, "pids.current") == 0 ||
         strcmp(last_slash + 1, "cgroup.freeze") == 0)) {
        *file_name = path + prefix_len + (last_slash - tmp) + 1;
        if (last_slash == tmp) {
            memcpy(visible, "/", 2);
        } else {
            *last_slash = '\0';
            if (strlen(tmp) >= visible_len) {
                return -ENAMETOOLONG;
            }
            memcpy(visible, tmp, strlen(tmp) + 1);
        }
        return 0;
    }
    if (strlen(suffix) >= visible_len) {
        return -ENAMETOOLONG;
    }
    memcpy(visible, suffix, strlen(suffix) + 1);
    return 0;
}

int cgroupfs_resolve_path(const char *path, char *cgroup_path, size_t cgroup_path_len,
                          enum cgroupfs_node_type *type_out) {
    struct task *task = task_current();
    char visible[MAX_PATH];
    char absolute[MAX_PATH];
    const char *file_name = NULL;
    struct cgroup *cgrp;
    enum cgroupfs_node_type type = CGROUPFS_NODE_NONE;

    if (!task || cgroupfs_visible_path(path, visible, sizeof(visible), &file_name) != 0 ||
        cgroup_absolute_from_visible(task, visible, absolute, sizeof(absolute)) != 0) {
        return -ENOENT;
    }
    kernel_mutex_lock(&cgroup_lock);
    cgrp = cgroup_find_absolute_locked(absolute);
    kernel_mutex_unlock(&cgroup_lock);
    if (!cgrp) {
        return -ENOENT;
    }
    if (!file_name) {
        type = CGROUPFS_NODE_DIR;
    } else if (strcmp(file_name, "cgroup.procs") == 0) {
        type = CGROUPFS_NODE_PROCS;
    } else if (strcmp(file_name, "cgroup.controllers") == 0) {
        type = CGROUPFS_NODE_CONTROLLERS;
    } else if (strcmp(file_name, "cgroup.subtree_control") == 0) {
        type = CGROUPFS_NODE_SUBTREE_CONTROL;
    } else if (strcmp(file_name, "pids.max") == 0) {
        type = CGROUPFS_NODE_PIDS_MAX;
    } else if (strcmp(file_name, "pids.current") == 0) {
        type = CGROUPFS_NODE_PIDS_CURRENT;
    } else if (strcmp(file_name, "cgroup.freeze") == 0) {
        type = CGROUPFS_NODE_FREEZE;
    }
    if (type == CGROUPFS_NODE_NONE) {
        return -ENOENT;
    }
    if (cgroup_path) {
        if (strlen(absolute) >= cgroup_path_len) {
            return -ENAMETOOLONG;
        }
        memcpy(cgroup_path, absolute, strlen(absolute) + 1);
    }
    if (type_out) {
        *type_out = type;
    }
    return 0;
}

enum cgroupfs_node_type cgroupfs_classify_path(const char *path) {
    enum cgroupfs_node_type type = CGROUPFS_NODE_NONE;

    if (cgroupfs_resolve_path(path, NULL, 0, &type) != 0) {
        return CGROUPFS_NODE_NONE;
    }
    return type;
}

int cgroupfs_mkdir(const char *path) {
    struct task *task = task_current();
    struct cgroup *parent;
    struct cgroup *child;
    char visible[MAX_PATH];
    char absolute[MAX_PATH];
    char parent_path[MAX_PATH];
    char *slash;

    if (!task || cgroupfs_visible_path(path, visible, sizeof(visible), NULL) != 0 ||
        strcmp(visible, "/") == 0 ||
        cgroup_absolute_from_visible(task, visible, absolute, sizeof(absolute)) != 0) {
        return -EINVAL;
    }
    if (!cgroupfs_current_can_control()) {
        return -EPERM;
    }
    if (cgroup_init() != 0) {
        return -ENOMEM;
    }

    if (strlen(absolute) >= sizeof(parent_path)) {
        return -ENAMETOOLONG;
    }
    memcpy(parent_path, absolute, strlen(absolute) + 1);
    slash = strrchr(parent_path, '/');
    if (!slash) {
        return -EINVAL;
    }
    if (strcmp(slash + 1, "cgroup.procs") == 0 ||
        strcmp(slash + 1, "cgroup.controllers") == 0 ||
        strcmp(slash + 1, "cgroup.subtree_control") == 0 ||
        strcmp(slash + 1, "pids.max") == 0 ||
        strcmp(slash + 1, "pids.current") == 0 ||
        strcmp(slash + 1, "cgroup.freeze") == 0) {
        return -EEXIST;
    }
    if (slash == parent_path) {
        parent_path[1] = '\0';
    } else {
        *slash = '\0';
    }

    kernel_mutex_lock(&cgroup_lock);
    if (cgroup_find_absolute_locked(absolute)) {
        kernel_mutex_unlock(&cgroup_lock);
        return -EEXIST;
    }
    parent = cgroup_find_absolute_locked(parent_path);
    if (!parent) {
        kernel_mutex_unlock(&cgroup_lock);
        return -ENOENT;
    }
    child = __kmalloc_noprof(sizeof(*child), GFP_KERNEL | __GFP_ZERO);
    if (!child) {
        kernel_mutex_unlock(&cgroup_lock);
        return -ENOMEM;
    }
    if (strlen(path) >= MAX_PATH) {
        kfree(child);
        kernel_mutex_unlock(&cgroup_lock);
        return -ENAMETOOLONG;
    }
    memcpy(child->path, absolute, strlen(absolute) + 1);
    atomic_set(&child->refs, 1);
    child->pids_max = -1;
    kernel_mutex_init(&child->lock);
    child->parent = parent;
    child->next_sibling = parent->children;
    parent->children = child;
    kernel_mutex_unlock(&cgroup_lock);
    return 0;
}

int cgroupfs_rmdir(const char *path) {
    struct task *task = task_current();
    struct cgroup *cgrp;
    struct cgroup *parent;
    struct cgroup **link;
    char visible[MAX_PATH];
    char absolute[MAX_PATH];
    unsigned int members;

    if (!task || cgroupfs_visible_path(path, visible, sizeof(visible), NULL) != 0 ||
        strcmp(visible, "/") == 0 ||
        cgroup_absolute_from_visible(task, visible, absolute, sizeof(absolute)) != 0) {
        return -EINVAL;
    }
    if (!cgroupfs_current_can_control()) {
        return -EPERM;
    }

    kernel_mutex_lock(&cgroup_lock);
    cgrp = cgroup_find_absolute_locked(absolute);
    if (!cgrp) {
        kernel_mutex_unlock(&cgroup_lock);
        return -ENOENT;
    }
    if (cgrp->children) {
        kernel_mutex_unlock(&cgroup_lock);
        return -ENOTEMPTY;
    }
    kernel_mutex_lock(&cgrp->lock);
    members = cgrp->members;
    kernel_mutex_unlock(&cgrp->lock);
    if (members > 0) {
        kernel_mutex_unlock(&cgroup_lock);
        return -EBUSY;
    }
    parent = cgrp->parent;
    if (!parent) {
        kernel_mutex_unlock(&cgroup_lock);
        return -EINVAL;
    }
    link = &parent->children;
    while (*link && *link != cgrp) {
        link = &(*link)->next_sibling;
    }
    if (*link != cgrp) {
        kernel_mutex_unlock(&cgroup_lock);
        return -ENOENT;
    }
    *link = cgrp->next_sibling;
    cgrp->parent = NULL;
    cgrp->next_sibling = NULL;
    kernel_mutex_unlock(&cgroup_lock);

    cgroup_put(cgrp);
    return 0;
}

static int cgroupfs_cgroup_for_path(const char *path, struct cgroup **out, enum cgroupfs_node_type *type_out) {
    char absolute[MAX_PATH];
    struct cgroup *cgrp;
    enum cgroupfs_node_type type;

    if (!out || cgroupfs_resolve_path(path, absolute, sizeof(absolute), &type) != 0) {
        return -EINVAL;
    }
    kernel_mutex_lock(&cgroup_lock);
    cgrp = cgroup_find_absolute_locked(absolute);
    if (cgrp) {
        cgroup_get(cgrp);
    }
    kernel_mutex_unlock(&cgroup_lock);
    if (!cgrp) {
        return -ENOENT;
    }
    *out = cgrp;
    if (type_out) {
        *type_out = type;
    }
    return 0;
}

static int cgroupfs_cgroup_for_absolute_path(const char *path, struct cgroup **out) {
    struct cgroup *cgrp;

    if (!path || !out || path[0] != '/') {
        return -EINVAL;
    }
    kernel_mutex_lock(&cgroup_lock);
    cgrp = cgroup_find_absolute_locked(path);
    if (cgrp) {
        cgroup_get(cgrp);
    }
    kernel_mutex_unlock(&cgroup_lock);
    if (!cgrp) {
        return -ENOENT;
    }
    *out = cgrp;
    return 0;
}

static int cgroup_append(char *buf, size_t buflen, size_t *pos, const char *text) {
    size_t len;

    if (!buf || !pos || !text) {
        return -EINVAL;
    }
    len = strlen(text);
    if (*pos + len >= buflen) {
        return -ENOSPC;
    }
    memcpy(buf + *pos, text, len);
    *pos += len;
    buf[*pos] = '\0';
    return 0;
}

static int cgroup_append_uint(char *buf, size_t buflen, size_t *pos, int32_t value) {
    char digits[16];
    size_t len = 0;
    uint32_t v = (uint32_t)value;

    do {
        digits[len++] = (char)('0' + (v % 10U));
        v /= 10U;
    } while (v != 0 && len < sizeof(digits));
    while (len > 0) {
        char c[2] = { digits[--len], '\0' };
        if (cgroup_append(buf, buflen, pos, c) != 0) {
            return -ENOSPC;
        }
    }
    return 0;
}

int cgroupfs_read_node(const char *cgroup_path, enum cgroupfs_node_type type,
                       char *buf, size_t buflen) {
    struct cgroup *cgrp = NULL;
    size_t pos = 0;
    int pids_max;
    bool frozen;
    bool subtree_pids;
    bool subtree_freezer;
    unsigned int members;

    if (!buf || buflen == 0) {
        return -EINVAL;
    }
    if (cgroupfs_cgroup_for_absolute_path(cgroup_path, &cgrp) != 0) {
        return -ENOENT;
    }
    buf[0] = '\0';
    if (type == CGROUPFS_NODE_CONTROLLERS) {
        if (cgroup_append(buf, buflen, &pos, "pids freezer\n") != 0) {
            cgroup_put(cgrp);
            return (int)pos;
        }
        cgroup_put(cgrp);
        return (int)pos;
    }
    if (type == CGROUPFS_NODE_SUBTREE_CONTROL) {
        kernel_mutex_lock(&cgrp->lock);
        subtree_pids = cgrp->subtree_pids;
        subtree_freezer = cgrp->subtree_freezer;
        kernel_mutex_unlock(&cgrp->lock);
        if (subtree_pids && cgroup_append(buf, buflen, &pos, "pids") != 0) {
            cgroup_put(cgrp);
            return (int)pos;
        }
        if (subtree_freezer) {
            if (pos > 0 && cgroup_append(buf, buflen, &pos, " ") != 0) {
                cgroup_put(cgrp);
                return (int)pos;
            }
            if (cgroup_append(buf, buflen, &pos, "freezer") != 0) {
                cgroup_put(cgrp);
                return (int)pos;
            }
        }
        if (pos > 0) {
            cgroup_append(buf, buflen, &pos, "\n");
        }
        cgroup_put(cgrp);
        return (int)pos;
    }
    if (type == CGROUPFS_NODE_PIDS_CURRENT) {
        kernel_mutex_lock(&cgrp->lock);
        members = cgrp->members;
        kernel_mutex_unlock(&cgrp->lock);
        if (cgroup_append_uint(buf, buflen, &pos, (int32_t)members) != 0 ||
            cgroup_append(buf, buflen, &pos, "\n") != 0) {
            cgroup_put(cgrp);
            return (int)pos;
        }
        cgroup_put(cgrp);
        return (int)pos;
    }
    if (type == CGROUPFS_NODE_PIDS_MAX) {
        kernel_mutex_lock(&cgrp->lock);
        pids_max = cgrp->pids_max;
        kernel_mutex_unlock(&cgrp->lock);
        if (pids_max < 0) {
            cgroup_append(buf, buflen, &pos, "max\n");
        } else if (cgroup_append_uint(buf, buflen, &pos, pids_max) == 0) {
            cgroup_append(buf, buflen, &pos, "\n");
        }
        cgroup_put(cgrp);
        return (int)pos;
    }
    if (type == CGROUPFS_NODE_FREEZE) {
        kernel_mutex_lock(&cgrp->lock);
        frozen = cgrp->freezer_frozen;
        kernel_mutex_unlock(&cgrp->lock);
        cgroup_append(buf, buflen, &pos, frozen ? "1\n" : "0\n");
        cgroup_put(cgrp);
        return (int)pos;
    }
    if (type != CGROUPFS_NODE_PROCS) {
        cgroup_put(cgrp);
        return -EISDIR;
    }
    kernel_mutex_lock(&task_table_lock);
    for (int i = 0; i < TASK_MAX_TASKS; i++) {
        for (struct task *task = task_table[i]; task; task = task->hash_next) {
            if (task->cgroup == cgrp) {
                if (cgroup_append_uint(buf, buflen, &pos, task->pid) != 0 ||
                    cgroup_append(buf, buflen, &pos, "\n") != 0) {
                    kernel_mutex_unlock(&task_table_lock);
                    cgroup_put(cgrp);
                    return (int)pos;
                }
            }
        }
    }
    kernel_mutex_unlock(&task_table_lock);
    cgroup_put(cgrp);
    return (int)pos;
}

int cgroupfs_read_path(const char *path, char *buf, size_t buflen) {
    char cgroup_path[MAX_PATH];
    enum cgroupfs_node_type type;

    if (cgroupfs_resolve_path(path, cgroup_path, sizeof(cgroup_path), &type) != 0) {
        return -ENOENT;
    }
    return cgroupfs_read_node(cgroup_path, type, buf, buflen);
}

static int cgroupfs_parse_nonnegative_int_or_max(const char *buf, size_t count, int *out) {
    int value = 0;
    bool saw_digit = false;

    if (!buf || !out) {
        return -EFAULT;
    }
    while (count > 0 && (*buf == ' ' || *buf == '\t' || *buf == '\n')) {
        buf++;
        count--;
    }
    if (count >= 3 && buf[0] == 'm' && buf[1] == 'a' && buf[2] == 'x') {
        *out = -1;
        return 0;
    }
    for (size_t i = 0; i < count; i++) {
        if (buf[i] >= '0' && buf[i] <= '9') {
            saw_digit = true;
            value = (value * 10) + (buf[i] - '0');
        } else if (buf[i] == '\n' || buf[i] == '\0' || buf[i] == ' ' || buf[i] == '\t') {
            break;
        } else {
            return -EINVAL;
        }
    }
    if (!saw_digit) {
        return -EINVAL;
    }
    *out = value;
    return 0;
}

static int cgroupfs_apply_subtree_control(struct cgroup *cgrp, const char *buf, size_t count) {
    size_t pos = 0;

    if (!cgrp || (!buf && count > 0)) {
        return -EFAULT;
    }
    while (pos < count) {
        bool enable;
        const char *name;
        size_t name_len;

        while (pos < count && (buf[pos] == ' ' || buf[pos] == '\t' || buf[pos] == '\n')) {
            pos++;
        }
        if (pos >= count) {
            break;
        }
        if (buf[pos] == '+') {
            enable = true;
        } else if (buf[pos] == '-') {
            enable = false;
        } else {
            return -EINVAL;
        }
        pos++;
        name = buf + pos;
        while (pos < count && buf[pos] != ' ' && buf[pos] != '\t' && buf[pos] != '\n' && buf[pos] != '\0') {
            pos++;
        }
        name_len = (size_t)(buf + pos - name);
        kernel_mutex_lock(&cgrp->lock);
        if (name_len == 4 && strncmp(name, "pids", 4) == 0) {
            cgrp->subtree_pids = enable;
        } else if (name_len == 7 && strncmp(name, "freezer", 7) == 0) {
            cgrp->subtree_freezer = enable;
        } else {
            kernel_mutex_unlock(&cgrp->lock);
            return -EINVAL;
        }
        kernel_mutex_unlock(&cgrp->lock);
    }
    return 0;
}

long cgroupfs_write_node(const char *cgroup_path, enum cgroupfs_node_type type,
                         const char *buf, size_t count) {
    struct task *writer = task_current();
    struct cgroup *target = NULL;
    int32_t pid = 0;
    struct task *task;
    int ret;
    int parsed;

    if (!buf && count > 0) {
        return -EFAULT;
    }
    if (!writer || !cgroupfs_current_can_control()) {
        return -EPERM;
    }
    if (cgroupfs_cgroup_for_absolute_path(cgroup_path, &target) != 0) {
        return -ENOENT;
    }
    if (type == CGROUPFS_NODE_PIDS_MAX) {
        ret = cgroupfs_parse_nonnegative_int_or_max(buf, count, &parsed);
        if (ret != 0) {
            cgroup_put(target);
            return ret;
        }
        kernel_mutex_lock(&target->lock);
        if (parsed >= 0 && target->members > (unsigned int)parsed) {
            kernel_mutex_unlock(&target->lock);
            cgroup_put(target);
            return -EBUSY;
        }
        target->pids_max = parsed;
        kernel_mutex_unlock(&target->lock);
        cgroup_put(target);
        return (long)count;
    }
    if (type == CGROUPFS_NODE_FREEZE) {
        ret = cgroupfs_parse_nonnegative_int_or_max(buf, count, &parsed);
        if (ret != 0 || (parsed != 0 && parsed != 1)) {
            cgroup_put(target);
            return ret != 0 ? ret : -EINVAL;
        }
        kernel_mutex_lock(&target->lock);
        target->freezer_frozen = parsed == 1;
        kernel_mutex_unlock(&target->lock);
        cgroup_put(target);
        return (long)count;
    }
    if (type == CGROUPFS_NODE_SUBTREE_CONTROL) {
        ret = cgroupfs_apply_subtree_control(target, buf, count);
        cgroup_put(target);
        return ret == 0 ? (long)count : ret;
    }
    if (type != CGROUPFS_NODE_PROCS) {
        cgroup_put(target);
        return -EINVAL;
    }
    for (size_t i = 0; i < count; i++) {
        if (buf[i] >= '0' && buf[i] <= '9') {
            pid = (pid * 10) + (buf[i] - '0');
        } else if (buf[i] == '\n' || buf[i] == '\0' || buf[i] == ' ') {
            break;
        } else {
            cgroup_put(target);
            return -EINVAL;
        }
    }
    if (pid == 0) {
        task = task_current();
        if (task) {
            atomic_inc(&task->refs);
        }
    } else {
        task = task_lookup(pid);
    }
    if (!task) {
        cgroup_put(target);
        return -ESRCH;
    }
    if (!cgroup_task_is_visible_to_task(writer, task)) {
        task_put(task);
        cgroup_put(target);
        return -EPERM;
    }
    ret = task_attach_cgroup(task, target);
    task_put(task);
    cgroup_put(target);
    if (ret != 0) {
        return ret;
    }
    return (long)count;
}

long cgroupfs_write_path(const char *path, const char *buf, size_t count) {
    char cgroup_path[MAX_PATH];
    enum cgroupfs_node_type type;

    if (cgroupfs_resolve_path(path, cgroup_path, sizeof(cgroup_path), &type) != 0) {
        return -ENOENT;
    }
    return cgroupfs_write_node(cgroup_path, type, buf, count);
}

size_t cgroupfs_child_count(const char *path) {
    struct cgroup *cgrp = NULL;
    size_t count = 0;
    const size_t fixed_count = 6;

    if (cgroupfs_cgroup_for_path(path, &cgrp, NULL) != 0) {
        return 0;
    }
    kernel_mutex_lock(&cgroup_lock);
    for (struct cgroup *child = cgrp->children; child; child = child->next_sibling) {
        count++;
    }
    kernel_mutex_unlock(&cgroup_lock);
    cgroup_put(cgrp);
    return count + fixed_count;
}

int cgroupfs_child_at(const char *path, size_t index, char *name, size_t name_len) {
    struct cgroup *cgrp = NULL;
    const char *fixed[] = {"cgroup.controllers", "cgroup.freeze", "cgroup.procs",
                           "cgroup.subtree_control", "pids.current", "pids.max"};
    const char *selected = NULL;
    size_t child_index;

    if (!name || name_len == 0 || cgroupfs_cgroup_for_path(path, &cgrp, NULL) != 0) {
        return -ENOENT;
    }
    if (index < sizeof(fixed) / sizeof(fixed[0])) {
        selected = fixed[index];
    } else {
        child_index = index - (sizeof(fixed) / sizeof(fixed[0]));
        kernel_mutex_lock(&cgroup_lock);
        for (struct cgroup *child = cgrp->children; child; child = child->next_sibling) {
            if (child_index == 0) {
                selected = cgroup_name(child);
                break;
            }
            child_index--;
        }
        kernel_mutex_unlock(&cgroup_lock);
    }
    cgroup_put(cgrp);
    if (!selected) {
        return -ENOENT;
    }
    if (strlen(selected) >= name_len) {
        return -ENAMETOOLONG;
    }
    memcpy(name, selected, strlen(selected) + 1);
    return 0;
}

int cgroup_proc_task_content(int32_t pid, char *buf, size_t buflen) {
    struct task *viewer = task_current();
    struct task *task;
    int ret;

    task = task_lookup(pid);
    if (!task) {
        return -ESRCH;
    }
    ret = task_cgroup_proc_content_for_viewer(viewer, task, buf, buflen);
    task_put(task);
    return ret;
}

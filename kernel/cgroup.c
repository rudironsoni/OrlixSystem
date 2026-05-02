/* IXLandSystem/kernel/cgroup.c
 * Virtual cgroup hierarchy and membership ownership.
 */

#include "cgroup.h"

#include "task.h"

#include <errno.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct cgroup {
    char path[MAX_PATH];
    atomic_int refs;
    unsigned int members;
    kernel_mutex_t lock;
    struct cgroup *parent;
    struct cgroup *children;
    struct cgroup *next_sibling;
};

static struct cgroup *root_cgroup;
static kernel_mutex_t cgroup_lock = KERNEL_MUTEX_INITIALIZER;

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
    free(cgrp);
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

static int cgroup_absolute_from_visible(const struct task_struct *task, const char *visible,
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
    ret = snprintf(out, out_len, "%s%s", root_path, visible);
    if (ret < 0) {
        return -EINVAL;
    }
    if ((size_t)ret >= out_len) {
        return -ENAMETOOLONG;
    }
    return 0;
}

static int cgroup_visible_path_for_task(const struct task_struct *task, char *out, size_t out_len) {
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

int cgroup_init(void) {
    kernel_mutex_lock(&cgroup_lock);
    if (root_cgroup) {
        kernel_mutex_unlock(&cgroup_lock);
        return 0;
    }

    root_cgroup = calloc(1, sizeof(*root_cgroup));
    if (!root_cgroup) {
        kernel_mutex_unlock(&cgroup_lock);
        return -ENOMEM;
    }

    memcpy(root_cgroup->path, "/", 2);
    atomic_init(&root_cgroup->refs, 1);
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
    atomic_fetch_add(&cgrp->refs, 1);
    return cgrp;
}

void cgroup_put(struct cgroup *cgrp) {
    if (!cgrp) {
        return;
    }
    if (atomic_fetch_sub(&cgrp->refs, 1) > 1) {
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

int task_attach_cgroup(struct task_struct *task, struct cgroup *cgrp) {
    struct cgroup *old;

    if (!task || !cgrp) {
        return -EINVAL;
    }

    old = task->cgroup;
    if (old == cgrp) {
        return 0;
    }

    cgroup_get(cgrp);
    kernel_mutex_lock(&cgrp->lock);
    cgrp->members++;
    kernel_mutex_unlock(&cgrp->lock);

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

int cgroup_attach_task_path(struct task_struct *task, const char *path) {
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

void task_detach_cgroup(struct task_struct *task) {
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

int task_unshare_cgroup_namespace(struct task_struct *task) {
    struct cgroup *old_root;

    if (!task || !task->cgroup) {
        return -EINVAL;
    }
    old_root = task->cgroup_ns_root;
    task->cgroup_ns_root = cgroup_get(task->cgroup);
    cgroup_put(old_root);
    return 0;
}

int task_reset_cgroup_namespace(struct task_struct *task) {
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
    cgroup_put(old_root);
    return 0;
}

const char *task_cgroup_path(const struct task_struct *task) {
    if (!task || !task->cgroup) {
        return "/";
    }
    return task->cgroup->path;
}

unsigned int task_cgroup_member_count(const struct task_struct *task) {
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

int task_cgroup_proc_content(const struct task_struct *task, char *buf, size_t buflen) {
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
    ret = snprintf(buf, buflen, "0::%s\n", visible);
    if (ret < 0) {
        return -EINVAL;
    }
    if ((size_t)ret >= buflen) {
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
         strcmp(last_slash + 1, "cgroup.subtree_control") == 0)) {
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

enum cgroupfs_node_type cgroupfs_classify_path(const char *path) {
    struct task_struct *task = get_current();
    char visible[MAX_PATH];
    char absolute[MAX_PATH];
    const char *file_name = NULL;
    struct cgroup *cgrp;

    if (!task || cgroupfs_visible_path(path, visible, sizeof(visible), &file_name) != 0 ||
        cgroup_absolute_from_visible(task, visible, absolute, sizeof(absolute)) != 0) {
        return CGROUPFS_NODE_NONE;
    }
    kernel_mutex_lock(&cgroup_lock);
    cgrp = cgroup_find_absolute_locked(absolute);
    kernel_mutex_unlock(&cgroup_lock);
    if (!cgrp) {
        return CGROUPFS_NODE_NONE;
    }
    if (!file_name) {
        return CGROUPFS_NODE_DIR;
    }
    if (strcmp(file_name, "cgroup.procs") == 0) {
        return CGROUPFS_NODE_PROCS;
    }
    if (strcmp(file_name, "cgroup.controllers") == 0) {
        return CGROUPFS_NODE_CONTROLLERS;
    }
    if (strcmp(file_name, "cgroup.subtree_control") == 0) {
        return CGROUPFS_NODE_SUBTREE_CONTROL;
    }
    return CGROUPFS_NODE_NONE;
}

int cgroupfs_mkdir(const char *path) {
    struct task_struct *task = get_current();
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
        strcmp(slash + 1, "cgroup.subtree_control") == 0) {
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
    child = calloc(1, sizeof(*child));
    if (!child) {
        kernel_mutex_unlock(&cgroup_lock);
        return -ENOMEM;
    }
    if (strlen(path) >= MAX_PATH) {
        free(child);
        kernel_mutex_unlock(&cgroup_lock);
        return -ENAMETOOLONG;
    }
    memcpy(child->path, absolute, strlen(absolute) + 1);
    atomic_init(&child->refs, 1);
    kernel_mutex_init(&child->lock);
    child->parent = parent;
    child->next_sibling = parent->children;
    parent->children = child;
    kernel_mutex_unlock(&cgroup_lock);
    return 0;
}

static int cgroupfs_cgroup_for_path(const char *path, struct cgroup **out, enum cgroupfs_node_type *type_out) {
    struct task_struct *task = get_current();
    char visible[MAX_PATH];
    char absolute[MAX_PATH];
    const char *file_name = NULL;
    struct cgroup *cgrp;

    if (!out || !task ||
        cgroupfs_visible_path(path, visible, sizeof(visible), &file_name) != 0 ||
        cgroup_absolute_from_visible(task, visible, absolute, sizeof(absolute)) != 0) {
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
        *type_out = cgroupfs_classify_path(path);
    }
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

int cgroupfs_read_path(const char *path, char *buf, size_t buflen) {
    struct cgroup *cgrp = NULL;
    enum cgroupfs_node_type type;
    size_t pos = 0;

    if (!buf || buflen == 0) {
        return -EINVAL;
    }
    if (cgroupfs_cgroup_for_path(path, &cgrp, &type) != 0) {
        return -ENOENT;
    }
    buf[0] = '\0';
    if (type == CGROUPFS_NODE_CONTROLLERS) {
        cgroup_put(cgrp);
        return 0;
    }
    if (type == CGROUPFS_NODE_SUBTREE_CONTROL) {
        cgroup_put(cgrp);
        return 0;
    }
    if (type != CGROUPFS_NODE_PROCS) {
        cgroup_put(cgrp);
        return -EISDIR;
    }
    kernel_mutex_lock(&task_table_lock);
    for (int i = 0; i < TASK_MAX_TASKS; i++) {
        for (struct task_struct *task = task_table[i]; task; task = task->hash_next) {
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

long cgroupfs_write_path(const char *path, const char *buf, size_t count) {
    struct cgroup *target = NULL;
    enum cgroupfs_node_type type;
    int32_t pid = 0;
    struct task_struct *task;
    int ret;

    if (!buf && count > 0) {
        return -EFAULT;
    }
    if (cgroupfs_cgroup_for_path(path, &target, &type) != 0) {
        return -ENOENT;
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
        task = get_current();
        if (task) {
            atomic_fetch_add(&task->refs, 1);
        }
    } else {
        task = task_lookup(pid);
    }
    if (!task) {
        cgroup_put(target);
        return -ESRCH;
    }
    ret = task_attach_cgroup(task, target);
    free_task(task);
    cgroup_put(target);
    if (ret != 0) {
        return ret;
    }
    return (long)count;
}

size_t cgroupfs_child_count(const char *path) {
    struct cgroup *cgrp = NULL;
    size_t count = 0;

    if (cgroupfs_cgroup_for_path(path, &cgrp, NULL) != 0) {
        return 0;
    }
    kernel_mutex_lock(&cgroup_lock);
    for (struct cgroup *child = cgrp->children; child; child = child->next_sibling) {
        count++;
    }
    kernel_mutex_unlock(&cgroup_lock);
    cgroup_put(cgrp);
    return count + 3;
}

int cgroupfs_child_at(const char *path, size_t index, char *name, size_t name_len) {
    struct cgroup *cgrp = NULL;
    const char *fixed[] = {"cgroup.controllers", "cgroup.procs", "cgroup.subtree_control"};
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
    struct task_struct *task;
    int ret;

    task = task_lookup(pid);
    if (!task) {
        return -ESRCH;
    }
    ret = task_cgroup_proc_content(task, buf, buflen);
    free_task(task);
    return ret;
}

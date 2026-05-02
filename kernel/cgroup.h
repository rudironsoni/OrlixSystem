/* IXLandSystem/kernel/cgroup.h
 * Private owner header for IXLand virtual cgroup state.
 *
 * This is runtime state, not Linux UAPI.
 */

#ifndef KERNEL_CGROUP_H
#define KERNEL_CGROUP_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct cgroup;
struct task_struct;

enum cgroupfs_node_type {
    CGROUPFS_NODE_NONE = 0,
    CGROUPFS_NODE_DIR,
    CGROUPFS_NODE_PROCS,
    CGROUPFS_NODE_CONTROLLERS,
    CGROUPFS_NODE_SUBTREE_CONTROL,
};

int cgroup_init(void);
void cgroup_deinit(void);
struct cgroup *cgroup_get(struct cgroup *cgrp);
void cgroup_put(struct cgroup *cgrp);
struct cgroup *cgroup_root(void);
int task_attach_cgroup(struct task_struct *task, struct cgroup *cgrp);
int cgroup_attach_task_path(struct task_struct *task, const char *path);
void task_detach_cgroup(struct task_struct *task);
int task_unshare_cgroup_namespace(struct task_struct *task);
int task_reset_cgroup_namespace(struct task_struct *task);
const char *task_cgroup_path(const struct task_struct *task);
unsigned int task_cgroup_member_count(const struct task_struct *task);
int task_cgroup_proc_content(const struct task_struct *task, char *buf, size_t buflen);
int cgroup_proc_task_content(int32_t pid, char *buf, size_t buflen);
enum cgroupfs_node_type cgroupfs_classify_path(const char *path);
int cgroupfs_mkdir(const char *path);
int cgroupfs_read_path(const char *path, char *buf, size_t buflen);
long cgroupfs_write_path(const char *path, const char *buf, size_t count);
size_t cgroupfs_child_count(const char *path);
int cgroupfs_child_at(const char *path, size_t index, char *name, size_t name_len);

#ifdef __cplusplus
}
#endif

#endif /* KERNEL_CGROUP_H */

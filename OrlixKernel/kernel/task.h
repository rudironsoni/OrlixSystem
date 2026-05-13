/* OrlixKernel/kernel/task.h
 * Private internal header for virtual task/process subsystem
 *
 * This is PRIVATE internal state for the virtual kernel's process model.
 * NOT a public Linux ABI header.
 *
 * Virtual task behavior emulated:
 * - virtual PID/TGID/PPID/PGID/SID namespace
 * - process groups and sessions
 * - parent/child relationships
 * - wait/reap semantics
 * - zombie/dead transitions
 * - clone/fork/vfork bookkeeping
 */

#ifndef KERNEL_TASK_H
#define KERNEL_TASK_H

#include <linux/limits.h>
#include <linux/types.h>
#include <linux/atomic.h>

#include "../fs/path.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TASK_COMM_CAPACITY 16
#define TASK_MAX_ARGS 256
#define TASK_MAX_TASKS 1024
#define TASK_EXEC_MAX_LOAD_SEGMENTS 16
#define TASK_EXEC_MAX_AUXV 32
#define TASK_EXEC_MAX_VMAS ((TASK_EXEC_MAX_LOAD_SEGMENTS * 2) + 2)
#define TASK_EXEC_MAX_DYNAMIC_NEEDED 16
#define TASK_VMA_PAGE_SIZE 4096ULL

/* Forward declarations for private subsystem state */
struct task;
struct task_vma;
struct fd_table;
struct fs_context;
struct signal_state;
struct tty_state;
struct memory_space;
struct vm_private_page;
struct vm_shared_mapping;
struct exec_image;
struct task_exec_handoff;
struct wait_queue_head;
struct nsproxy;
struct uts_state;
struct cgroup;
struct seccomp_policy;
struct cred;
struct task_rlimit;
struct clone_args;
struct address_space;

enum task_restart_kind {
    TASK_RESTART_NONE = 0,
    TASK_RESTART_NANOSLEEP,
    TASK_RESTART_POLL,
    TASK_RESTART_PIPE_READ,
    TASK_RESTART_PIPE_WRITE,
    TASK_RESTART_FUTEX_WAIT,
    TASK_RESTART_WAITPID,
    TASK_RESTART_SELECT,
    TASK_RESTART_EPOLL_WAIT,
};

/* Task global table - virtual PID namespace */
extern struct task *task_table[TASK_MAX_TASKS];

/* The task_init_process (pid 1) - set up during kernel boot */
extern struct task *task_init_process;

/* Task allocation - virtual kernel internal */
struct task *alloc_task(void);
void task_put(struct task *task);

/* Current task accessors - virtual kernel runtime */
struct task *task_current(void);
void task_set_current(struct task *task);

/* Virtual PID namespace management */
int32_t task_alloc_pid(void);
int pid_reserve(int32_t pid);
void task_free_pid(int32_t pid);
void pid_init(void);

/* Virtual task table management */
int task_init(void);
void task_deinit(void);
struct task *task_lookup(int32_t pid);
int task_hash(int32_t pid);
int task_reassign_pid_impl(struct task *task, int32_t pid);
struct task *task_create_child_impl(struct task *parent);
struct task *task_create_child_with_flags_impl(struct task *parent, uint64_t flags);
void task_unlink_child_impl(struct task *parent, struct task *child);
void task_mark_stopped_by_signal(struct task *task, int32_t sig);
void task_mark_continued_by_signal(struct task *task);
void task_mark_signaled_exit(struct task *task, int32_t sig);
void task_mark_exited(struct task *task, int status);
void task_notify_parent_state_change(struct task *task);
/* Virtual process identity syscalls (internal helpers) */
int32_t getpid_impl(void);
int32_t getppid_impl(void);
int32_t getpgrp_impl(void);
int32_t getpgid_impl(int32_t pid);
int setpgid_impl(int32_t pid, int32_t pgid);
int32_t getsid_impl(int32_t pid);
int32_t setsid_impl(void);
int task_session_has_pgrp_impl(int32_t sid, int32_t pgid);

/* Virtual fork/exec - internal helpers */
int32_t fork_impl(void);
int32_t vfork_impl(void);
int32_t clone_impl(uint64_t flags);
int32_t clone3_impl(const struct clone_args *args, size_t size);
int unshare_impl(uint64_t flags);
int task_exec_transition_impl(const char *path, const char *argv0);

/* Virtual exit helper */
void exit_impl(int status);

/* Virtual vfork notifications */
void vfork_exec_notify(void);
void vfork_exit_notify(void);

#ifdef __cplusplus
}
#endif

#endif /* KERNEL_TASK_H */

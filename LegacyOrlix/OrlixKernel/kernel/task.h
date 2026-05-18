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

/* Virtual process identity syscalls (internal helpers) */
int32_t getpid_impl(void);
int32_t getppid_impl(void);
int32_t getpgrp_impl(void);
int32_t getpgid_impl(int32_t pid);
int setpgid_impl(int32_t pid, int32_t pgid);
int32_t getsid_impl(int32_t pid);
int32_t setsid_impl(void);

/* Virtual fork/exec - internal helpers */
int32_t fork_impl(void);
int32_t vfork_impl(void);
int32_t clone_impl(uint64_t flags);
int32_t clone3_impl(const struct clone_args *args, size_t size);
int unshare_impl(uint64_t flags);

/* Virtual exit helper */
void exit_impl(int status);

#ifdef __cplusplus
}
#endif

#endif /* KERNEL_TASK_H */

/* OrlixKernel/kernel/seccomp.h
 * Private owner header for Orlix virtual syscall policy.
 */

#ifndef KERNEL_SECCOMP_H
#define KERNEL_SECCOMP_H

#include <linux/types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct seccomp_policy;
struct task;

struct seccomp_policy *seccomp_alloc(void);
struct seccomp_policy *seccomp_get(struct seccomp_policy *policy);
void seccomp_put(struct seccomp_policy *policy);

int seccomp_set_task_errno_policy(struct task *task, int64_t syscall_nr, int err);
int seccomp_set_thread_group_errno_policy(struct task *task, int64_t syscall_nr, int err);
void seccomp_clear_task_policy(struct task *task);
long seccomp_check_current_syscall(int64_t syscall_nr);

#ifdef __cplusplus
}
#endif

#endif /* KERNEL_SECCOMP_H */

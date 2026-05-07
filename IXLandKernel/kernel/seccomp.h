/* IXLandKernel/kernel/seccomp.h
 * Private owner header for IXLand virtual syscall policy.
 */

#ifndef KERNEL_SECCOMP_H
#define KERNEL_SECCOMP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct seccomp;
struct task_struct;

struct seccomp *seccomp_alloc(void);
struct seccomp *seccomp_get(struct seccomp *policy);
void seccomp_put(struct seccomp *policy);

int seccomp_set_task_errno_policy(struct task_struct *task, int64_t syscall_nr, int err);
int seccomp_set_thread_group_errno_policy(struct task_struct *task, int64_t syscall_nr, int err);
void seccomp_clear_task_policy(struct task_struct *task);
long seccomp_check_current_syscall(int64_t syscall_nr);

#ifdef __cplusplus
}
#endif

#endif /* KERNEL_SECCOMP_H */

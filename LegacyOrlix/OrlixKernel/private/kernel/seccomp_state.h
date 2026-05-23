#ifndef PRIVATE_KERNEL_SECCOMP_STATE_H
#define PRIVATE_KERNEL_SECCOMP_STATE_H

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

#endif /* PRIVATE_KERNEL_SECCOMP_STATE_H */

#ifndef KERNEL_FUTEX_H
#define KERNEL_FUTEX_H

#ifdef __cplusplus
extern "C" {
#endif

int futex_wait_impl(int *uaddr, int expected, int timeout_ms);
int futex_wake_impl(int *uaddr, int max_wake);
int futex_op_impl(int *uaddr, int futex_op, int val, int timeout_ms);
int set_robust_list_impl(void *head, unsigned long len);
int get_robust_list_impl(int pid, void **head, unsigned long *len);
struct task_struct;
void futex_task_exit_impl(struct task_struct *task);

#ifdef __cplusplus
}
#endif

#endif

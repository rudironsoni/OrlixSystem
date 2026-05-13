#ifndef PRIVATE_KERNEL_FUTEX_STATE_H
#define PRIVATE_KERNEL_FUTEX_STATE_H

#include "kernel/futex.h"

#ifdef __cplusplus
extern "C" {
#endif

struct task;

void futex_reset_impl(void);
void futex_task_exit_impl(struct task *task);

#ifdef __cplusplus
}
#endif

#endif /* PRIVATE_KERNEL_FUTEX_STATE_H */

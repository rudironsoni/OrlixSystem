#ifndef IXLAND_INTERNAL_PRIVATE_TASK_CURRENT_CONTRACT_H
#define IXLAND_INTERNAL_PRIVATE_TASK_CURRENT_CONTRACT_H

#ifdef __cplusplus
extern "C" {
#endif

struct task_struct;

void set_current(struct task_struct *task);

#ifdef __cplusplus
}
#endif

#endif

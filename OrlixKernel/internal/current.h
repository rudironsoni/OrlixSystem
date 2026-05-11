#ifndef INTERNAL_CURRENT_H
#define INTERNAL_CURRENT_H

#ifdef __cplusplus
extern "C" {
#endif

struct task_struct;

void set_current(struct task_struct *task);

#ifdef __cplusplus
}
#endif

#endif

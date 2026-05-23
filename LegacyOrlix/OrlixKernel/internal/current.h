#ifndef INTERNAL_CURRENT_H
#define INTERNAL_CURRENT_H

#ifdef __cplusplus
extern "C" {
#endif

struct task;

void task_set_current(struct task *task);

#ifdef __cplusplus
}
#endif

#endif

#ifndef INTERNAL_CURRENT_H
#define INTERNAL_CURRENT_H

#ifdef __cplusplus
extern "C" {
#endif

struct task;

void set_current_task(struct task *task);

#ifdef __cplusplus
}
#endif

#endif

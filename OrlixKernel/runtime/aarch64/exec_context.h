/* OrlixKernel/runtime/aarch64/exec_context.h
 * Private aarch64 execution handoff substrate for virtual Linux tasks.
 */

#ifndef RUNTIME_AARCH64_EXEC_CONTEXT_H
#define RUNTIME_AARCH64_EXEC_CONTEXT_H

#include <linux/types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct task_struct;

struct aarch64_exec_context {
    u64 pc;
    u64 sp;
    u64 steps;
    u32 stopped;
    struct task_struct *task;
    long (*read_memory)(struct task_struct *task, u64 addr, void *buf, size_t count);
    long (*write_memory)(struct task_struct *task, u64 addr, const void *buf, size_t count);
};

int aarch64_exec_context_from_task(struct task_struct *task, struct aarch64_exec_context *context);
int aarch64_exec_context_step(struct aarch64_exec_context *context);
int aarch64_exec_context_run(struct aarch64_exec_context *context, u64 max_steps, u64 *out_steps);

#ifdef __cplusplus
}
#endif

#endif /* RUNTIME_AARCH64_EXEC_CONTEXT_H */

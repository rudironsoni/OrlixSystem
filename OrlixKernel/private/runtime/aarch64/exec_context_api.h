/* OrlixKernel/private/runtime/aarch64/exec_context_api.h
 * Private aarch64 execution handoff substrate for virtual Linux tasks.
 */

#ifndef PRIVATE_RUNTIME_AARCH64_EXEC_CONTEXT_API_H
#define PRIVATE_RUNTIME_AARCH64_EXEC_CONTEXT_API_H

#include <linux/types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct task;

struct aarch64_exec_context;

int aarch64_exec_context_from_task(struct task *task, struct aarch64_exec_context *context);
int aarch64_exec_context_step(struct aarch64_exec_context *context);
int aarch64_exec_context_run(struct aarch64_exec_context *context, u64 max_steps, u64 *out_steps);

#ifdef __cplusplus
}
#endif

#endif /* PRIVATE_RUNTIME_AARCH64_EXEC_CONTEXT_API_H */

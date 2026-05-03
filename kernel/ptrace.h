/* IXLandSystem/kernel/ptrace.h
 * Private owner header for virtual ptrace supervision state.
 *
 * This is runtime behavior over IXLand tasks, not host process debugging.
 */

#ifndef KERNEL_PTRACE_H
#define KERNEL_PTRACE_H

#include <asm/posix_types.h>

#ifdef __cplusplus
extern "C" {
#endif

long ptrace_impl(long request, __kernel_pid_t pid, void *addr, void *data);

#ifdef __cplusplus
}
#endif

#endif /* KERNEL_PTRACE_H */

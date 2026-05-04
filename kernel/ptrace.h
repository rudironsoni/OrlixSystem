/* IXLandSystem/kernel/ptrace.h
 * Private owner header for virtual ptrace supervision state.
 *
 * This is runtime behavior over IXLand tasks, not host process debugging.
 */

#ifndef KERNEL_PTRACE_H
#define KERNEL_PTRACE_H

#include <asm/posix_types.h>
#include <stdint.h>

struct task_struct;

#ifdef __cplusplus
extern "C" {
#endif

long ptrace_impl(long request, __kernel_pid_t pid, void *addr, void *data);
int ptrace_may_access_task_impl(const struct task_struct *tracer, const struct task_struct *target);
int ptrace_note_syscall_entry(long number, long arg0, long arg1, long arg2,
                              long arg3, long arg4, long arg5);
void ptrace_note_syscall_exit(long retval);
void ptrace_note_fork_event(struct task_struct *task, __kernel_pid_t child_pid, int clone_event);
void ptrace_note_exec_event(struct task_struct *task);
void ptrace_note_exit_event(struct task_struct *task, int status);
int ptrace_note_signal_delivery(struct task_struct *task, int32_t sig);

#ifdef __cplusplus
}
#endif

#endif /* KERNEL_PTRACE_H */

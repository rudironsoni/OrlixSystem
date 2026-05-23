#ifndef PRIVATE_KERNEL_PTRACE_STATE_H
#define PRIVATE_KERNEL_PTRACE_STATE_H

#include "kernel/ptrace.h"

#ifdef __cplusplus
extern "C" {
#endif

int ptrace_may_access_task_impl(const struct task *tracer, const struct task *target);
int ptrace_note_syscall_entry(long number, long arg0, long arg1, long arg2,
                              long arg3, long arg4, long arg5);
void ptrace_note_syscall_exit(long retval);
void ptrace_note_fork_event(struct task *task, __kernel_pid_t child_pid, int clone_event);
void ptrace_rewrite_fork_event_message(struct task *task, __kernel_pid_t old_child_pid,
                                       __kernel_pid_t new_child_pid, int clone_event);
void ptrace_note_exec_event(struct task *task);
void ptrace_note_exit_event(struct task *task, int status);
int ptrace_note_signal_delivery(struct task *task, __s32 sig);

#ifdef __cplusplus
}
#endif

#endif /* PRIVATE_KERNEL_PTRACE_STATE_H */

/* OrlixKernel/kernel/signal.h
 * Private internal header for virtual signal subsystem
 *
 * This is PRIVATE internal state for the virtual kernel's signal handling.
 * NOT a public Linux ABI header.
 *
 * Virtual signal behavior emulated:
 * - standard and realtime signals
 * - per-task signal masks
 * - pending signals
 * - process-directed vs thread-directed delivery
 * - fork inheriting signal mask
 * - handler installation via sigaction
 * - sigprocmask, sigpending, sigsuspend, kill, killpg, raise
 */

#ifndef KERNEL_SIGNAL_H
#define KERNEL_SIGNAL_H

#include <linux/atomic.h>
#include <linux/signal.h>
#include <linux/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declaration - avoid circular include with task.h */
struct task;
struct signal_state;

int kernel_thread_sigmask(int how, const sigset_t *set, sigset_t *oldset);
int kernel_sigemptyset(sigset_t *set);
int kernel_sigaddset(sigset_t *set, int signo);
int kernel_sigismember(sigset_t *set, int signo);

int do_sigaction(int sig, const struct sigaction *act, struct sigaction *oldact);
int do_sigprocmask(int how, const sigset_t *set, sigset_t *oldset);
int do_sigsetmask(const sigset_t *set, sigset_t *oldset);
int do_sigpending(sigset_t *set);
int do_signal(int signum, __sighandler_t handler, __sighandler_t *old_handler);
int do_raise(int sig);
int do_pause(void);
int do_sigsuspend(const sigset_t *mask);
int do_kill(int pid, int sig);
int do_killpg(int pgrp, int sig);

/* Internal signal generation */
int signal_generate_task(struct task *target, int32_t sig);
int signal_generate_task_info(struct task *target, int32_t sig, int32_t code, u64 addr);
int signal_generate_process(struct task *target, int32_t sig);
int signal_send_process(struct task *target, int32_t sig);
int signal_generate_pgrp(int32_t pgid, int32_t sig);
int signal_generate_orphaned_pgrp(int32_t pgid);

/* Check if signal is blocked */
bool signal_is_blocked(const struct task *task, int32_t sig);
bool signal_is_pending(const struct task *task, int32_t sig);
bool signal_has_unblocked_pending(const struct task *task);

int do_sigaltstack(const stack_t *new_stack, stack_t *old_stack);

#ifdef __cplusplus
}
#endif

#endif /* KERNEL_SIGNAL_H */

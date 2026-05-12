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
#include <linux/types.h>

#include "../include/signal_calls.h"
#include "internal/mutex.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declaration - avoid circular include with task.h */
struct task;

int kernel_thread_sigmask(int how, const struct signal_mask_bits *set,
                          struct signal_mask_bits *oldset);
int kernel_sigemptyset(struct signal_mask_bits *set);
int kernel_sigaddset(struct signal_mask_bits *set, int signo);
int kernel_sigismember(const struct signal_mask_bits *set, int signo);

/* Signal queue entry - private internal */
struct signal_queue_entry {
    int32_t sig;
    int32_t si_signo;
    int32_t si_errno;
    int32_t si_code;
    u64 fault_addr;
    struct signal_queue_entry *next;
};

/* Signal queue - private internal */
struct signal_queue {
    struct signal_queue_entry *head;
    struct signal_queue_entry *tail;
    int count;
    kernel_mutex_t lock;
};

/* Signal stack state - private internal */
struct signal_altstack {
    void *ss_sp;
    size_t ss_size;
    int32_t ss_flags;
};

/* Signal handler table - per-task signal configuration
 * This is the private internal state, NOT the public ABI struct sigaction */
struct signal_state {
    atomic_t refs;
    struct signal_action_slot actions[KERNEL_SIG_NUM];
    struct signal_mask_bits blocked;
    struct signal_mask_bits pending;
    struct signal_mask_bits shared_pending;
    struct signal_queue queue;
    struct signal_altstack altstack;
    kernel_mutex_t lock;
};

/* Signal pending state for a task */
struct pending_signals {
    struct signal_mask_bits signal;
    struct signal_queue queue;
};

/* Initialize signal state for a new task */
int signal_init_task(struct task *task);

/* Inherit signal state on fork/clone */
struct signal_state *alloc_signal_struct(void);
void free_signal_struct(struct signal_state *sig);
struct signal_state *dup_signal_struct(struct signal_state *parent);

/* Reset signal state on exec */
void signal_reset_on_exec(struct task *task);

/* Virtual signal enqueue helpers */
int signal_enqueue_task(struct task *task, int32_t sig);
int signal_enqueue_group(int32_t pgid, int32_t sig);
int signal_dequeue(struct task *task, struct signal_mask_bits *mask, int32_t *sig);

/* Recompute pending state after mask changes */
void signal_recompute_pending(struct task *task);

/* Signal wakeup - wake the right task after signal generation */
void signal_wake_task(struct task *task, bool group_wide);

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

int do_sigaltstack(const struct signal_altstack *new_stack, struct signal_altstack *old_stack);
int signal_prepare_frame_impl(struct task *task, int32_t sig, u64 return_pc,
                              u64 current_sp, u64 *frame_sp_out);

#ifdef __cplusplus
}
#endif

#endif /* KERNEL_SIGNAL_H */

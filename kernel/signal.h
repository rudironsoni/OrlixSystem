/* IXLandSystem/kernel/signal.h
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


#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>

#include "../internal/ios/kernel/sync.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declaration - avoid circular include with task.h */
struct task_struct;

/* Signal count - Linux uses 64 signals */
#define KERNEL_SIG_NUM 64
#define KERNEL_SIG_NUM_WORDS ((KERNEL_SIG_NUM + 63) / 64)

/* Signal handler type - private internal */
typedef void (*sighandler_t)(int);

struct signal_mask_bits {
    uint64_t sig[KERNEL_SIG_NUM_WORDS];
};

/* Signal queue entry - private internal */
struct signal_queue_entry {
    int32_t sig;
    int32_t si_signo;
    int32_t si_errno;
    int32_t si_code;
    struct signal_queue_entry *next;
};

/* Signal queue - private internal */
struct signal_queue {
    struct signal_queue_entry *head;
    struct signal_queue_entry *tail;
    int count;
    kernel_mutex_t lock;
};

/* Signal action slot - private internal
 * Storage for one signal's handler configuration */
struct signal_action_slot {
    sighandler_t handler;
    struct signal_mask_bits mask;
    int32_t flags;
};

/* Signal stack state - private internal */
struct signal_altstack {
    void *ss_sp;
    size_t ss_size;
    int32_t ss_flags;
};

/* Signal handler table - per-task signal configuration
 * This is the private internal state, NOT the public ABI struct sigaction */
struct signal_struct {
    atomic_int refs;
    struct signal_action_slot actions[KERNEL_SIG_NUM];
    struct signal_mask_bits blocked;
    struct signal_mask_bits pending;
    struct signal_queue queue;
    struct signal_altstack altstack;
    kernel_mutex_t lock;
};

/* Signal pending state for a task */
struct sigpending {
    struct signal_mask_bits signal;
    struct signal_queue queue;
};

/* Initialize signal state for a new task */
int signal_init_task(struct task_struct *task);

/* Inherit signal state on fork/clone */
struct signal_struct *alloc_signal_struct(void);
void free_signal_struct(struct signal_struct *sig);
struct signal_struct *dup_signal_struct(struct signal_struct *parent);

/* Reset signal state on exec */
void signal_reset_on_exec(struct task_struct *task);

/* Virtual signal enqueue helpers */
int signal_enqueue_task(struct task_struct *task, int32_t sig);
int signal_enqueue_group(int32_t pgid, int32_t sig);
int signal_dequeue(struct task_struct *task, struct signal_mask_bits *mask, int32_t *sig);

/* Recompute pending state after mask changes */
void signal_recompute_pending(struct task_struct *task);

/* Signal wakeup - wake the right task after signal generation */
void signal_wake_task(struct task_struct *task, bool group_wide);

/* Internal signal generation */
int signal_generate_task(struct task_struct *target, int32_t sig);
int signal_generate_pgrp(int32_t pgid, int32_t sig);

/* Check if signal is blocked */
bool signal_is_blocked(const struct task_struct *task, int32_t sig);
bool signal_is_pending(const struct task_struct *task, int32_t sig);
bool signal_has_unblocked_pending(const struct task_struct *task);

/* ============================================================================
 * INTERNAL SYSCALL IMPLEMENTATIONS (for host-bridge use)
 * These are the internal implementations that the Darwin bridge calls.
 * ============================================================================
 */
int do_sigaction(int32_t sig, const struct signal_action_slot *act,
                 struct signal_action_slot *oldact);
int do_sigprocmask(int how, const struct signal_mask_bits *set,
                   struct signal_mask_bits *oldset);
int do_sigpending(struct signal_mask_bits *set);
sighandler_t do_signal(int32_t signum, sighandler_t handler);
int do_raise(int32_t sig);
int do_pause(void);
int do_sigsuspend(const struct signal_mask_bits *mask);
int do_kill(int32_t pid, int32_t sig);
int do_killpg(int32_t pgrp, int32_t sig);
int do_sigaltstack(const struct signal_altstack *new_stack, struct signal_altstack *old_stack);
int signal_prepare_frame_impl(struct task_struct *task, int32_t sig, uint64_t return_pc,
                              uint64_t current_sp, uint64_t *frame_sp_out);

#ifdef __cplusplus
}
#endif

#endif /* KERNEL_SIGNAL_H */

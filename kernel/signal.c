/* IXLandSystem/kernel/signal.c
 * Internal kernel signal owner implementation
 *
 * Public wrappers use proper POSIX types.
 * Internal logic uses private types only.
 */

#include <string.h>
#include <errno.h>
#include <stdlib.h>

#ifdef SIGHUP
#undef SIGHUP
#endif
#ifdef SIGINT
#undef SIGINT
#endif
#ifdef SIGQUIT
#undef SIGQUIT
#endif
#ifdef SIGILL
#undef SIGILL
#endif
#ifdef SIGTRAP
#undef SIGTRAP
#endif
#ifdef SIGABRT
#undef SIGABRT
#endif
#ifdef SIGIOT
#undef SIGIOT
#endif
#ifdef SIGBUS
#undef SIGBUS
#endif
#ifdef SIGFPE
#undef SIGFPE
#endif
#ifdef SIGKILL
#undef SIGKILL
#endif
#ifdef SIGUSR1
#undef SIGUSR1
#endif
#ifdef SIGSEGV
#undef SIGSEGV
#endif
#ifdef SIGUSR2
#undef SIGUSR2
#endif
#ifdef SIGPIPE
#undef SIGPIPE
#endif
#ifdef SIGALRM
#undef SIGALRM
#endif
#ifdef SIGTERM
#undef SIGTERM
#endif
#ifdef SIGCHLD
#undef SIGCHLD
#endif
#ifdef SIGCONT
#undef SIGCONT
#endif
#ifdef SIGSTOP
#undef SIGSTOP
#endif
#ifdef SIGTSTP
#undef SIGTSTP
#endif
#ifdef SIGTTIN
#undef SIGTTIN
#endif
#ifdef SIGTTOU
#undef SIGTTOU
#endif
#ifdef SIGURG
#undef SIGURG
#endif
#ifdef SIGXCPU
#undef SIGXCPU
#endif
#ifdef SIGXFSZ
#undef SIGXFSZ
#endif
#ifdef SIGVTALRM
#undef SIGVTALRM
#endif
#ifdef SIGPROF
#undef SIGPROF
#endif
#ifdef SIGWINCH
#undef SIGWINCH
#endif
#ifdef SIGIO
#undef SIGIO
#endif
#ifdef SIGPOLL
#undef SIGPOLL
#endif
#ifdef SIGPWR
#undef SIGPWR
#endif
#ifdef SIGSYS
#undef SIGSYS
#endif
#ifdef SIGUNUSED
#undef SIGUNUSED
#endif
#ifdef SIGRTMIN
#undef SIGRTMIN
#endif
#ifdef SIGRTMAX
#undef SIGRTMAX
#endif
#ifdef SIG_BLOCK
#undef SIG_BLOCK
#endif
#ifdef SIG_UNBLOCK
#undef SIG_UNBLOCK
#endif
#ifdef SIG_SETMASK
#undef SIG_SETMASK
#endif
#ifdef SIG_DFL
#undef SIG_DFL
#endif
#ifdef SIG_IGN
#undef SIG_IGN
#endif
#ifdef SIG_ERR
#undef SIG_ERR
#endif
#include <linux/types.h>
#define __ASSEMBLY__ 1
#include <asm-generic/signal.h>
#undef __ASSEMBLY__
#include <asm-generic/signal-defs.h>
typedef struct {
    unsigned long sig[1];
} k_sigset_frame;
typedef struct {
    void *ss_sp;
    int ss_flags;
    size_t ss_size;
} k_stack_frame;
#ifdef __unused
#undef __unused
#endif
#define sigset_t k_sigset_frame
#define stack_t k_stack_frame
#include <asm/sigcontext.h>
#include <asm/ucontext.h>
#undef stack_t
#undef sigset_t

#include "signal.h"
#include "task.h"
#include "wait_queue.h"

enum {
    frame_record_words = 12,
    frame_ucontext_offset = 128,
};

struct signal_struct *alloc_signal_struct(void) {
    struct signal_struct *sig = calloc(1, sizeof(struct signal_struct));
    if (!sig)
        return NULL;

    atomic_init(&sig->refs, 1);
    kernel_mutex_init(&sig->queue.lock);

    /* Initialize default handlers (SIG_DFL = NULL) */
    for (int i = 0; i < KERNEL_SIG_NUM; i++) {
        sig->actions[i].handler = NULL;
        memset(&sig->actions[i].mask, 0, sizeof(struct signal_mask_bits));
        sig->actions[i].flags = 0;
    }

    memset(&sig->blocked, 0, sizeof(struct signal_mask_bits));
    memset(&sig->pending, 0, sizeof(struct signal_mask_bits));
    memset(&sig->shared_pending, 0, sizeof(struct signal_mask_bits));
    sig->altstack.ss_sp = NULL;
    sig->altstack.ss_size = 0;
    sig->altstack.ss_flags = 2;

    return sig;
}

void free_signal_struct(struct signal_struct *sig) {
    if (!sig)
        return;
    if (atomic_fetch_sub(&sig->refs, 1) > 1)
        return;

    /* Free queued signals */
    kernel_mutex_lock(&sig->queue.lock);
    struct signal_queue_entry *entry = sig->queue.head;
    while (entry) {
        struct signal_queue_entry *next = entry->next;
        free(entry);
        entry = next;
    }
    kernel_mutex_unlock(&sig->queue.lock);

    kernel_mutex_destroy(&sig->queue.lock);
    free(sig);
}

struct signal_struct *dup_signal_struct(struct signal_struct *parent) {
    if (!parent)
        return NULL;

    struct signal_struct *child = alloc_signal_struct();
    if (!child)
        return NULL;

    /* Copy signal handlers */
    memcpy(child->actions, parent->actions, sizeof(child->actions));

    /* Child inherits parent's signal mask */
    child->blocked = parent->blocked;

    /* But pending signals are cleared */
    memset(&child->pending, 0, sizeof(struct signal_mask_bits));
    memset(&child->shared_pending, 0, sizeof(struct signal_mask_bits));

    return child;
}

static int signal_queue_append(struct signal_struct *signal, int32_t sig, int32_t code, uint64_t addr) {
    struct signal_queue_entry *entry;

    if (!signal) {
        return -EINVAL;
    }

    entry = calloc(1, sizeof(*entry));
    if (!entry) {
        return -ENOMEM;
    }

    entry->sig = sig;
    entry->si_signo = sig;
    entry->si_errno = 0;
    entry->si_code = code;
    entry->fault_addr = addr;

    kernel_mutex_lock(&signal->queue.lock);
    if (sig < SIGRTMIN) {
        struct signal_queue_entry *prev = NULL;
        for (struct signal_queue_entry *cursor = signal->queue.head; cursor; cursor = cursor->next) {
            if (cursor->sig == sig) {
                if (prev) {
                    prev->next = cursor->next;
                } else {
                    signal->queue.head = cursor->next;
                }
                if (signal->queue.tail == cursor) {
                    signal->queue.tail = prev;
                }
                signal->queue.count--;
                free(cursor);
                break;
            }
            prev = cursor;
        }
    }
    if (signal->queue.tail) {
        signal->queue.tail->next = entry;
    } else {
        signal->queue.head = entry;
    }
    signal->queue.tail = entry;
    signal->queue.count++;
    kernel_mutex_unlock(&signal->queue.lock);

    return 0;
}

static int signal_queue_remove_first(struct signal_struct *signal, int32_t sig) {
    struct signal_queue_entry *prev = NULL;
    struct signal_queue_entry *entry;
    int remains = 0;

    if (!signal) {
        return 0;
    }

    kernel_mutex_lock(&signal->queue.lock);
    entry = signal->queue.head;
    while (entry) {
        struct signal_queue_entry *next = entry->next;
        if (entry->sig == sig) {
            if (prev) {
                prev->next = next;
            } else {
                signal->queue.head = next;
            }
            if (signal->queue.tail == entry) {
                signal->queue.tail = prev;
            }
            signal->queue.count--;
            free(entry);
            break;
        }
        prev = entry;
        entry = next;
    }
    for (entry = signal->queue.head; entry; entry = entry->next) {
        if (entry->sig == sig) {
            remains = 1;
            break;
        }
    }
    kernel_mutex_unlock(&signal->queue.lock);
    return remains;
}

static bool signal_default_action_is_ignore(int32_t sig) {
    switch (sig) {
    case SIGURG:
    case SIGWINCH:
        return true;
    default:
        return false;
    }
}

static bool signal_action_is_ignored(const struct task_struct *task, int32_t sig) {
    sighandler_t handler;

    if (!task || !task->signal || sig < 1 || sig > KERNEL_SIG_NUM) {
        return false;
    }
    handler = task->signal->actions[sig - 1].handler;
    if (handler == SIG_IGN) {
        return true;
    }
    if (handler == SIG_DFL && signal_default_action_is_ignore(sig)) {
        return true;
    }
    return false;
}

static bool signal_default_action_is_terminate(int32_t sig) {
    switch (sig) {
    case SIGTERM:
    case SIGKILL:
    case SIGINT:
    case SIGQUIT:
        return true;
    default:
        return false;
    }
}

static bool signal_action_is_default(const struct task_struct *task, int32_t sig) {
    if (!task || !task->signal || sig < 1 || sig > KERNEL_SIG_NUM) {
        return true;
    }
    return task->signal->actions[sig - 1].handler == SIG_DFL;
}

static int apply_signal_to_task_pending(struct task_struct *task, int32_t sig, int32_t code,
                                        uint64_t addr, bool shared) {
    int queued;

    if (!task || !task->signal)
        return -EINVAL;

    if (signal_action_is_ignored(task, sig) && sig != SIGCONT) {
        return 0;
    }

    queued = signal_queue_append(task->signal, sig, code, addr);
    if (queued != 0) {
        return queued;
    }

    /* Mark signal as pending - use 0-based indexing (sig - 1) */
    int idx = (sig - 1) / 64;
    int bit = (sig - 1) % 64;
    if (idx < KERNEL_SIG_NUM_WORDS && sig >= 1 && sig <= KERNEL_SIG_NUM) {
        if (shared) {
            task->signal->shared_pending.sig[idx] |= (1ULL << bit);
        } else {
            task->thread_pending_signals |= (1ULL << bit);
        }
    }

    if (sig == SIGSTOP || sig == SIGTSTP || sig == SIGTTIN || sig == SIGTTOU) {
        task_mark_stopped_by_signal(task, sig);
        task_notify_parent_state_change(task);
    } else if (sig == SIGCONT) {
        task_mark_continued_by_signal(task);
        task_notify_parent_state_change(task);
    } else if (signal_default_action_is_terminate(sig) &&
               (sig == SIGKILL || signal_action_is_default(task, sig))) {
        task_mark_signaled_exit(task, sig);
        task_notify_parent_state_change(task);
    }

    signal_wake_task(task, false);
    return 0;
}

static int apply_signal_to_task(struct task_struct *task, int32_t sig, int32_t code, uint64_t addr) {
    return apply_signal_to_task_pending(task, sig, code, addr, false);
}

int signal_generate_task(struct task_struct *target, int32_t sig) {
    return signal_generate_task_info(target, sig, 0, 0);
}

int signal_generate_task_info(struct task_struct *target, int32_t sig, int32_t code, uint64_t addr) {
    int result;

    if (!target || sig < 1 || sig > KERNEL_SIG_NUM)
        return -EINVAL;

    if (sig == 0) {
        /* Just check if process exists */
        return 0;
    }

    kernel_mutex_lock(&target->lock);
    result = apply_signal_to_task(target, sig, code, addr);
    kernel_mutex_unlock(&target->lock);

    return result;
}

int signal_generate_process(struct task_struct *target, int32_t sig) {
    struct task_struct *selected;
    int result;

    if (!target || sig < 1 || sig > KERNEL_SIG_NUM) {
        return -EINVAL;
    }

    selected = target;
    kernel_mutex_lock(&task_table_lock);
    for (int i = 0; i < TASK_MAX_TASKS; i++) {
        struct task_struct *task = task_table[i];
        while (task) {
            if (task->tgid == target->tgid && !signal_is_blocked(task, sig)) {
                selected = task;
                i = TASK_MAX_TASKS;
                break;
            }
            task = task->hash_next;
        }
    }
    kernel_mutex_unlock(&task_table_lock);

    kernel_mutex_lock(&selected->lock);
    result = apply_signal_to_task_pending(selected, sig, 0, 0, true);
    kernel_mutex_unlock(&selected->lock);
    return result;
}

int signal_generate_pgrp(int32_t pgid, int32_t sig) {
    struct task_struct *targets[TASK_MAX_TASKS];
    int target_count = 0;

    if (sig < 0 || sig > KERNEL_SIG_NUM)
        return -EINVAL;

    /* Convert negative pgid to positive (killpg semantics) */
    if (pgid < 0)
        pgid = -pgid;

    if (pgid <= 0)
        return -ESRCH;

    kernel_mutex_lock(&task_table_lock);

    for (int i = 0; i < TASK_MAX_TASKS; i++) {
        struct task_struct *task = task_table[i];
        while (task) {
            if (task->pgid == pgid) {
                atomic_fetch_add(&task->refs, 1);
                if (target_count < TASK_MAX_TASKS) {
                    targets[target_count++] = task;
                } else {
                    free_task(task);
                }
            }
            task = task->hash_next;
        }
    }

    kernel_mutex_unlock(&task_table_lock);

    if (target_count == 0)
        return -ESRCH;

    for (int i = 0; i < target_count; i++) {
        int result;

        kernel_mutex_lock(&targets[i]->lock);
        result = apply_signal_to_task_pending(targets[i], sig, 0, 0, true);
        kernel_mutex_unlock(&targets[i]->lock);
        free_task(targets[i]);
        if (result != 0) {
            for (int j = i + 1; j < target_count; j++) {
                free_task(targets[j]);
            }
            return result;
        }
    }

    return 0;
}

int signal_enqueue_task(struct task_struct *task, int32_t sig) {
    return signal_generate_task(task, sig);
}

int signal_enqueue_group(int32_t pgid, int32_t sig) {
    return signal_generate_pgrp(pgid, sig);
}

int signal_dequeue(struct task_struct *task, struct signal_mask_bits *mask, int32_t *sig) {
    if (!task || !task->signal || !sig)
        return -EINVAL;

    /* Find first pending signal that's not blocked */
    for (int i = 1; i <= KERNEL_SIG_NUM; i++) {
        /* Use 0-based indexing (sig - 1) */
        int idx = (i - 1) / 64;
        int bit = (i - 1) % 64;

        if (idx >= KERNEL_SIG_NUM_WORDS)
            continue;

        /* Check if pending and not blocked */
        if (((task->thread_pending_signals | task->signal->shared_pending.sig[idx]) & (1ULL << bit)) &&
            !(task->signal->blocked.sig[idx] & (1ULL << bit))) {

            /* Also check against provided mask if given */
            if (mask && (mask->sig[idx] & (1ULL << bit)))
                continue;

            *sig = i;
            /* Clear pending bit */
            if (!signal_queue_remove_first(task->signal, i)) {
                task->thread_pending_signals &= ~(1ULL << bit);
                task->signal->shared_pending.sig[idx] &= ~(1ULL << bit);
            }
            return 1; /* Return 1 to indicate signal found */
        }
    }

    return -EAGAIN;
}

void signal_recompute_pending(struct task_struct *task) {
    /* Recompute whether task has any deliverable pending signals */
    if (!task || !task->signal)
        return;

    /* This would update any cached "has_pending" flags */
    /* For now, pending signals are checked on demand */
}

void signal_wake_task(struct task_struct *task, bool group_wide) {
    struct wait_queue_head *queue;

    (void)group_wide;

    if (!task)
        return;

    /* Wake the task if it's waiting */
    kernel_mutex_lock(&task->wait_lock);
    queue = task->current_wait_queue;
    if (task->waiters > 0) {
        kernel_cond_broadcast(&task->wait_cond);
    }
    kernel_mutex_unlock(&task->wait_lock);

    if (queue) {
        wait_queue_wake_all(queue);
    }
}

bool signal_is_blocked(const struct task_struct *task, int32_t sig) {
    if (!task || !task->signal)
        return false;

    /* Use 0-based indexing (sig - 1) */
    int idx = (sig - 1) / 64;
    int bit = (sig - 1) % 64;

    if (idx >= KERNEL_SIG_NUM_WORDS)
        return false;

    return (task->signal->blocked.sig[idx] & (1ULL << bit)) != 0;
}

bool signal_is_pending(const struct task_struct *task, int32_t sig) {
    if (!task || !task->signal || sig < 1 || sig > KERNEL_SIG_NUM)
        return false;

    int idx = (sig - 1) / 64;
    int bit = (sig - 1) % 64;

    if (idx >= KERNEL_SIG_NUM_WORDS)
        return false;

    return ((task->thread_pending_signals | task->signal->shared_pending.sig[idx]) &
            (1ULL << bit)) != 0;
}

bool signal_has_unblocked_pending(const struct task_struct *task) {
    if (!task || !task->signal) {
        return false;
    }

    for (int sig = 1; sig <= KERNEL_SIG_NUM; sig++) {
        int idx = (sig - 1) / 64;
        int bit = (sig - 1) % 64;

        if (idx >= KERNEL_SIG_NUM_WORDS) {
            continue;
        }

        if (((task->thread_pending_signals | task->signal->shared_pending.sig[idx]) & (1ULL << bit)) &&
            !(task->signal->blocked.sig[idx] & (1ULL << bit))) {
            return true;
        }
    }

    return false;
}

void signal_reset_on_exec(struct task_struct *task) {
    if (!task || !task->signal)
        return;

    /* Reset signal handlers that have SA_RESETHAND flag set */
    /* For now, simplified: reset pending signals */
    memset(&task->signal->pending, 0, sizeof(struct signal_mask_bits));
    memset(&task->signal->shared_pending, 0, sizeof(struct signal_mask_bits));
    task->thread_pending_signals = 0;
}

int signal_init_task(struct task_struct *task) {
    if (!task)
        return -EINVAL;

    task->signal = alloc_signal_struct();
    if (!task->signal)
        return -ENOMEM;

    return 0;
}

/* ============================================================================
 * INTERNAL SIGNAL SYSCALL IMPLEMENTATIONS
 * ============================================================================
 * These use only private internal types from kernel/signal.h
 */

int do_sigaction(int32_t sig, const struct signal_action_slot *act,
                 struct signal_action_slot *oldact) {
    if (sig < 1 || sig >= KERNEL_SIG_NUM) {
        errno = EINVAL;
        return -1;
    }

    if (sig == SIGKILL || sig == SIGSTOP) {
        errno = EINVAL;
        return -1;
    }

    struct task_struct *task = get_current();
    if (!task || !task->signal) {
        errno = ESRCH;
        return -1;
    }

    if (oldact) {
        *oldact = task->signal->actions[sig - 1];
    }

    if (act) {
        task->signal->actions[sig - 1] = *act;
    }

    return 0;
}

int do_sigprocmask(int how, const struct signal_mask_bits *set,
		   struct signal_mask_bits *oldset) {
	struct task_struct *task = get_current();
	if (!task || !task->signal) {
		errno = ESRCH;
		return -1;
	}

	struct signal_struct *sig = task->signal;

	if (oldset) {
		*oldset = sig->blocked;
	}

	if (set) {
		switch (how) {
        case SIG_BLOCK: /* Block signals in set */
			for (int i = 0; i < KERNEL_SIG_NUM_WORDS; i++) {
				sig->blocked.sig[i] |= set->sig[i];
			}
			break;
        case SIG_UNBLOCK: /* Unblock signals in set */
			for (int i = 0; i < KERNEL_SIG_NUM_WORDS; i++) {
				sig->blocked.sig[i] &= ~set->sig[i];
			}
			break;
        case SIG_SETMASK: /* Replace blocked set with set */
			sig->blocked = *set;
			break;
		default:
			errno = EINVAL;
			return -1;
		}
	}

	return 0;
}

int do_sigsetmask(const struct signal_mask_bits *set, struct signal_mask_bits *oldset) {
    return do_sigprocmask(SIG_SETMASK, set, oldset);
}

int do_sigpending(struct signal_mask_bits *set) {
    if (!set) {
        errno = EFAULT;
        return -1;
    }

    struct task_struct *task = get_current();
    if (!task || !task->signal) {
        errno = ESRCH;
        return -1;
    }

    memset(set, 0, sizeof(*set));
    set->sig[0] = task->thread_pending_signals;
    for (int i = 0; i < KERNEL_SIG_NUM_WORDS; i++) {
        set->sig[i] |= task->signal->shared_pending.sig[i];
    }
    return 0;
}

sighandler_t do_signal(int32_t signum, sighandler_t handler) {
    if (signum < 1 || signum >= KERNEL_SIG_NUM) {
        errno = EINVAL;
        return NULL;
    }

    if (signum == SIGKILL || signum == SIGSTOP) {
        errno = EINVAL;
        return NULL;
    }

    struct task_struct *task = get_current();
    if (!task || !task->signal) {
        errno = ESRCH;
        return NULL;
    }

    sighandler_t old_handler = task->signal->actions[signum - 1].handler;
    task->signal->actions[signum - 1].handler = handler;
    task->signal->actions[signum - 1].flags = 0;
    memset(&task->signal->actions[signum - 1].mask, 0, sizeof(struct signal_mask_bits));

    return old_handler;
}

int do_raise(int32_t sig) {
    struct task_struct *task = get_current();
    if (!task) {
        errno = ESRCH;
        return -1;
    }
    return signal_generate_task(task, sig);
}

static int is_sigset_empty(const struct signal_mask_bits *set) {
    for (int i = 0; i < KERNEL_SIG_NUM_WORDS; i++) {
        if (set->sig[i] != 0)
            return 0;
    }
    return 1;
}

int do_pause(void) {
    struct task_struct *task = get_current();
    if (!task) {
        errno = ESRCH;
        return -1;
    }

    kernel_mutex_lock(&task->wait_lock);

    while (task->thread_pending_signals == 0 && is_sigset_empty(&task->signal->shared_pending)) {
        task->waiters++;
        kernel_cond_wait(&task->wait_cond, &task->wait_lock);
        task->waiters--;
    }

    kernel_mutex_unlock(&task->wait_lock);

    errno = EINTR;
    return -1;
}

int do_sigsuspend(const struct signal_mask_bits *mask) {
    struct task_struct *task = get_current();
    if (!task || !task->signal) {
        errno = ESRCH;
        return -1;
    }

    if (!mask) {
        errno = EFAULT;
        return -1;
    }

    /* Save old mask */
    struct signal_mask_bits old_mask = task->signal->blocked;

    /* Install new mask */
    task->signal->blocked = *mask;

    /* Wait for signal */
    kernel_mutex_lock(&task->wait_lock);
    task->waiters++;
    kernel_cond_wait(&task->wait_cond, &task->wait_lock);
    task->waiters--;
    kernel_mutex_unlock(&task->wait_lock);

    /* Restore old mask */
    task->signal->blocked = old_mask;

    errno = EINTR;
    return -1;
}

int do_kill(int32_t pid, int32_t sig) {
    if (sig < 0 || sig > KERNEL_SIG_NUM) {
        errno = EINVAL;
        return -1;
    }

    int result;

    if (pid <= 0) {
        /* Process group handling */
        if (pid == 0) {
            /* Current process group */
            struct task_struct *task = get_current();
            if (!task) {
                errno = ESRCH;
                return -1;
            }
            result = signal_generate_pgrp(task->pgid, sig);
        } else if (pid == -1) {
            /* All processes (privileged) */
            errno = EPERM;
            return -1;
        } else {
            /* Process group |pid| */
            result = signal_generate_pgrp(-pid, sig);
        }
        /* Convert internal error codes to errno + return -1 */
        if (result < 0) {
            errno = -result;
            return -1;
        }
        return result;
    }

    struct task_struct *task = task_lookup(pid);
    if (!task) {
        errno = ESRCH;
        return -1;
    }

    if (sig == 0) {
        /* Just check if process exists */
        free_task(task);
        return 0;
    }

    result = signal_generate_process(task, sig);
    free_task(task);
    /* Convert internal error codes to errno + return -1 */
    if (result < 0) {
        errno = -result;
        return -1;
    }
    return result;
}

int do_killpg(int32_t pgrp, int32_t sig) {
    int result = signal_generate_pgrp(pgrp, sig);
    /* Convert internal error codes to errno + return -1 */
    if (result < 0) {
        errno = -result;
        return -1;
    }
    return result;
}

int do_sigaltstack(const struct signal_altstack *new_stack, struct signal_altstack *old_stack) {
    struct task_struct *task = get_current();

    if (!task || !task->signal) {
        errno = ESRCH;
        return -1;
    }
    if (old_stack) {
        *old_stack = task->signal->altstack;
    }
    if (new_stack) {
        if ((new_stack->ss_flags & ~(2 | (int32_t)(1U << 31))) != 0) {
            errno = EINVAL;
            return -1;
        }
        if ((new_stack->ss_flags & 2) == 0 && new_stack->ss_size < 5120) {
            errno = ENOMEM;
            return -1;
        }
        task->signal->altstack = *new_stack;
    }
    return 0;
}

int signal_prepare_frame_impl(struct task_struct *task, int32_t sig, uint64_t return_pc,
                              uint64_t current_sp, uint64_t *frame_sp_out) {
    uint64_t frame_sp;
    uint64_t frame_record[frame_record_words];
    struct ucontext context;
    size_t frame_size = frame_ucontext_offset + sizeof(context);
    const struct signal_action_slot *action;

    if (!task || !task->signal || !task->mm || sig < 1 || sig > KERNEL_SIG_NUM || !frame_sp_out) {
        errno = EINVAL;
        return -1;
    }
    action = &task->signal->actions[sig - 1];
    frame_sp = current_sp;
    if ((task->signal->altstack.ss_flags & 2) == 0 && task->signal->altstack.ss_sp &&
        task->signal->altstack.ss_size >= 5120) {
        frame_sp = (uint64_t)(uintptr_t)task->signal->altstack.ss_sp + task->signal->altstack.ss_size;
        frame_sp &= ~15ULL;
        task->signal->altstack.ss_flags |= 1;
    }
    if (frame_sp < frame_size) {
        errno = EFAULT;
        return -1;
    }
    frame_sp -= frame_size;
    frame_sp &= ~15ULL;
    memset(frame_record, 0, sizeof(frame_record));
    frame_record[0] = (uint64_t)sig;
    frame_record[1] = return_pc;
    frame_record[2] = (uint64_t)(uintptr_t)action->handler;
    frame_record[3] = (uint64_t)(uint32_t)action->flags;
    frame_record[4] = task->signal->blocked.sig[0];
    frame_record[5] = (uint64_t)(uintptr_t)task->signal->altstack.ss_sp;
    frame_record[6] = (uint64_t)task->signal->altstack.ss_size;
    frame_record[7] = (uint64_t)(uint32_t)task->signal->altstack.ss_flags;
    frame_record[8] = current_sp;
    frame_record[9] = frame_sp;
    frame_record[10] = task->signal->blocked.sig[0];
    frame_record[11] = frame_size;
    if (task_write_virtual_memory_impl(task, frame_sp, frame_record, sizeof(frame_record)) !=
        (long)sizeof(frame_record)) {
        return -1;
    }
    memset(&context, 0, sizeof(context));
    context.uc_flags = 1;
    context.uc_link = NULL;
    context.uc_stack.ss_sp = task->signal->altstack.ss_sp;
    context.uc_stack.ss_size = task->signal->altstack.ss_size;
    context.uc_stack.ss_flags = task->signal->altstack.ss_flags;
    context.uc_sigmask.sig[0] = task->signal->blocked.sig[0];
    context.uc_mcontext.sp = current_sp;
    context.uc_mcontext.pc = return_pc;
    if (task_write_virtual_memory_impl(task, frame_sp + frame_ucontext_offset,
                                       &context, sizeof(context)) != (long)sizeof(context)) {
        return -1;
    }
    task->mm->signal_frame_sp = frame_sp;
    task->mm->signal_frame_signo = (uint64_t)sig;
    task->mm->signal_frame_return_pc = return_pc;
    task->mm->signal_handler_pc = (uint64_t)(uintptr_t)action->handler;
    task->mm->signal_frame_flags = (uint64_t)(uint32_t)action->flags;
    task->mm->signal_frame_restorer_pc = action->restorer;
    task->mm->signal_frame_mask = task->signal->blocked.sig[0];
    task->mm->signal_frame_altstack_sp = (uint64_t)(uintptr_t)task->signal->altstack.ss_sp;
    task->mm->signal_frame_altstack_size = (uint64_t)task->signal->altstack.ss_size;
    task->mm->signal_frame_altstack_flags = (uint64_t)(uint32_t)task->signal->altstack.ss_flags;
    task->mm->signal_frame_current_sp = current_sp;
    task->mm->signal_frame_size = frame_size;
    task->mm->signal_frame_ucontext_flags = 1;
    for (int i = 0; i < KERNEL_SIG_NUM_WORDS; i++) {
        task->signal->blocked.sig[i] |= action->mask.sig[i];
    }
    if ((action->flags & SA_NODEFER) == 0) {
        task->signal->blocked.sig[(sig - 1) >> 6] |= (1ULL << ((sig - 1) & 63));
    }
    if ((action->flags & SA_RESETHAND) != 0 && sig != SIGKILL && sig != SIGSTOP) {
        task->signal->actions[sig - 1].handler = SIG_DFL;
        task->signal->actions[sig - 1].flags = 0;
        task->signal->actions[sig - 1].restorer = 0;
        memset(&task->signal->actions[sig - 1].mask, 0,
               sizeof(task->signal->actions[sig - 1].mask));
    }
    *frame_sp_out = frame_sp;
    return 0;
}

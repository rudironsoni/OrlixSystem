/* OrlixKernel/kernel/signal.c
 * Internal kernel signal owner implementation
 *
 * Public wrappers use proper POSIX types.
 * Internal logic uses private types only.
 */

#include <linux/errno.h>
#include <linux/gfp_types.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/capability.h>
#include <uapi/linux/signal.h>
#include <uapi/asm/sigcontext.h>
#include <uapi/asm/ucontext.h>

#include "../private/kernel/signal_state.h"
#include "../private/kernel/task_state.h"
#include "../private/kernel/cred_state.h"
#include "signal.h"
#include "cred.h"
#include "ptrace.h"
#include "task.h"
#include "wait_queue.h"

extern void *__kmalloc_noprof(size_t size, gfp_t flags);
extern void kfree(const void *objp);

enum {
    frame_record_words = 12,
    frame_ucontext_offset = 128,
};

int kernel_thread_sigmask(int how, const sigset_t *set, sigset_t *oldset) {
    (void)how;
    (void)set;
    (void)oldset;
    return 0;
}

int kernel_sigemptyset(sigset_t *set) {
    if (!set) {
        return -EINVAL;
    }
    sigemptyset(set);
    return 0;
}

int kernel_sigaddset(sigset_t *set, int signo) {
    if (!set || signo <= 0 || signo > KERNEL_SIG_NUM) {
        return -EINVAL;
    }
    sigaddset(set, signo);
    return 0;
}

int kernel_sigismember(sigset_t *set, int signo) {
    if (!set || signo <= 0 || signo > KERNEL_SIG_NUM) {
        return -EINVAL;
    }
    return sigismember(set, signo);
}

struct signal_state *alloc_signal_struct(void) {
    struct signal_state *sig = __kmalloc_noprof(sizeof(struct signal_state), GFP_KERNEL | __GFP_ZERO);
    if (!sig)
        return NULL;

    atomic_set(&sig->refs, 1);
    kernel_mutex_init(&sig->queue.lock);

    /* Initialize default handlers (SIG_DFL = NULL) */
    for (int i = 0; i < KERNEL_SIG_NUM; i++) {
        sig->actions[i].sa_handler = SIG_DFL;
        sigemptyset(&sig->actions[i].sa_mask);
        sig->actions[i].sa_flags = 0;
        sig->actions[i].sa_restorer = 0;
    }

    sigemptyset(&sig->blocked);
    sigemptyset(&sig->pending);
    sigemptyset(&sig->shared_pending);
    sig->altstack.ss_sp = NULL;
    sig->altstack.ss_size = 0;
    sig->altstack.ss_flags = 2;

    return sig;
}

void free_signal_struct(struct signal_state *sig) {
    if (!sig)
        return;
    if (atomic_dec_return(&sig->refs) > 0)
        return;

    /* Free queued signals */
    kernel_mutex_lock(&sig->queue.lock);
    struct signal_queue_entry *entry = sig->queue.head;
    while (entry) {
        struct signal_queue_entry *next = entry->next;
        kfree(entry);
        entry = next;
    }
    kernel_mutex_unlock(&sig->queue.lock);

    kernel_mutex_destroy(&sig->queue.lock);
    kfree(sig);
}

struct signal_state *dup_signal_struct(struct signal_state *parent) {
    if (!parent)
        return NULL;

    struct signal_state *child = alloc_signal_struct();
    if (!child)
        return NULL;

    /* Copy signal handlers */
    memcpy(child->actions, parent->actions, sizeof(child->actions));

    /* Child inherits parent's signal mask */
    child->blocked = parent->blocked;

    /* But pending signals are cleared */
    sigemptyset(&child->pending);
    sigemptyset(&child->shared_pending);

    return child;
}

static int signal_queue_append(struct signal_state *signal, int32_t sig, int32_t code, uint64_t addr) {
    struct signal_queue_entry *entry;

    if (!signal) {
        return -EINVAL;
    }

    entry = __kmalloc_noprof(sizeof(*entry), GFP_KERNEL | __GFP_ZERO);
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
                kfree(cursor);
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

static int signal_queue_remove_first(struct signal_state *signal, int32_t sig) {
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
            kfree(entry);
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

static bool signal_action_is_ignored(const struct task *task, int32_t sig) {
    __sighandler_t handler;

    if (!task || !task->signal || sig < 1 || sig > KERNEL_SIG_NUM) {
        return false;
    }
    handler = task->signal->actions[sig - 1].sa_handler;
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
    case SIGHUP:
    case SIGTERM:
    case SIGKILL:
    case SIGINT:
    case SIGQUIT:
    case SIGPIPE:
        return true;
    default:
        return false;
    }
}

static bool signal_action_is_default(const struct task *task, int32_t sig) {
    if (!task || !task->signal || sig < 1 || sig > KERNEL_SIG_NUM) {
        return true;
    }
    return task->signal->actions[sig - 1].sa_handler == SIG_DFL;
}

static int apply_signal_to_task_pending(struct task *task, int32_t sig, int32_t code,
                                        uint64_t addr, bool shared) {
    int queued;

    if (!task || !task->signal)
        return -EINVAL;

    if (signal_action_is_ignored(task, sig) && sig != SIGCONT) {
        return 0;
    }

    if (ptrace_note_signal_delivery(task, sig) != 0) {
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

static int apply_signal_to_task(struct task *task, int32_t sig, int32_t code, uint64_t addr) {
    return apply_signal_to_task_pending(task, sig, code, addr, false);
}

static bool signal_sender_may_target(const struct task *sender,
                                     const struct task *target) {
    uint64_t target_user_ns_id;

    if (!sender || !target || !sender->cred || !target->cred) {
        return false;
    }
    if (sender->pid == target->pid) {
        return true;
    }

    target_user_ns_id = cred_user_namespace_id(target->cred);
    if (cred_has_cap_in_user_namespace(sender->cred, target_user_ns_id, CAP_KILL)) {
        return true;
    }

    return sender->cred->euid == target->cred->uid ||
           sender->cred->euid == target->cred->suid ||
           sender->cred->uid == target->cred->uid ||
           sender->cred->uid == target->cred->suid;
}

int signal_generate_task(struct task *target, int32_t sig) {
    return signal_generate_task_info(target, sig, 0, 0);
}

int signal_generate_task_info(struct task *target, int32_t sig, int32_t code, uint64_t addr) {
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

int signal_generate_process(struct task *target, int32_t sig) {
    struct task *selected;
    int result;

    if (!target || sig < 1 || sig > KERNEL_SIG_NUM) {
        return -EINVAL;
    }

    selected = target;
    kernel_mutex_lock(&task_table_lock);
    for (int i = 0; i < TASK_MAX_TASKS; i++) {
        struct task *task = task_table[i];
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

int signal_send_process(struct task *target, int32_t sig) {
    struct task *sender = task_current();

    if (!target || sig < 0 || sig > KERNEL_SIG_NUM) {
        return -EINVAL;
    }
    if (!sender) {
        return -ESRCH;
    }
    if (atomic_read(&target->exited)) {
        return -ESRCH;
    }
    if (!signal_sender_may_target(sender, target)) {
        return -EPERM;
    }
    if (sig == 0) {
        return 0;
    }

    return signal_generate_process(target, sig);
}

int signal_generate_pgrp(int32_t pgid, int32_t sig) {
    struct task *targets[TASK_MAX_TASKS];
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
        struct task *task = task_table[i];
        while (task) {
            if (task->pgid == pgid) {
                atomic_inc(&task->refs);
                if (target_count < TASK_MAX_TASKS) {
                    targets[target_count++] = task;
                } else {
                    task_put(task);
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
        task_put(targets[i]);
        if (result != 0) {
            for (int j = i + 1; j < target_count; j++) {
                task_put(targets[j]);
            }
            return result;
        }
    }

    return 0;
}

int signal_generate_orphaned_pgrp(int32_t pgid) {
    int result;

    result = signal_generate_pgrp(pgid, SIGHUP);
    if (result != 0) {
        return result;
    }
    return signal_generate_pgrp(pgid, SIGCONT);
}

int signal_enqueue_task(struct task *task, int32_t sig) {
    return signal_generate_task(task, sig);
}

int signal_enqueue_group(int32_t pgid, int32_t sig) {
    return signal_generate_pgrp(pgid, sig);
}

int signal_dequeue(struct task *task, sigset_t *mask, int32_t *sig) {
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

void signal_clear_queued_task(struct task *task, int32_t sig) {
    struct signal_queue_entry *prev = NULL;
    struct signal_queue_entry *entry;

    if (!task || !task->signal || sig < 1 || sig > KERNEL_SIG_NUM) {
        return;
    }

    kernel_mutex_lock(&task->signal->queue.lock);
    entry = task->signal->queue.head;
    while (entry) {
        struct signal_queue_entry *next = entry->next;
        if (entry->sig == sig) {
            if (prev) {
                prev->next = next;
            } else {
                task->signal->queue.head = next;
            }
            if (task->signal->queue.tail == entry) {
                task->signal->queue.tail = prev;
            }
            task->signal->queue.count--;
            kfree(entry);
        } else {
            prev = entry;
        }
        entry = next;
    }
    kernel_mutex_unlock(&task->signal->queue.lock);

    signal_clear_pending_task(task, sig);
}

void signal_clear_next_pending_task(struct task *task, int32_t sig) {
    sigset_t mask;
    int32_t delivered;

    if (!task || sig < 1 || sig > KERNEL_SIG_NUM) {
        return;
    }
    sigemptyset(&mask);
    while (signal_dequeue(task, &mask, &delivered) == 1) {
        if (delivered != sig) {
            signal_generate_task(task, delivered);
            break;
        }
    }
}

void signal_clear_pending_markers_task(struct task *task, int32_t sig) {
    if (!task || !task->signal || sig < 1 || sig > KERNEL_SIG_NUM) {
        return;
    }
    task->thread_pending_signals &= ~(1ULL << ((sig - 1) & 63));
    task->signal->pending.sig[(sig - 1) >> 6] &= ~(1ULL << ((sig - 1) & 63));
    task->signal->shared_pending.sig[(sig - 1) >> 6] &= ~(1ULL << ((sig - 1) & 63));
}

void signal_clear_pending_task(struct task *task, int32_t sig) {
    int32_t dequeued = 0;

    if (!task || !task->signal || sig < 1 || sig > KERNEL_SIG_NUM) {
        return;
    }
    while (signal_dequeue(task, NULL, &dequeued) > 0) {
        if (dequeued == sig) {
            break;
        }
    }
    signal_clear_pending_markers_task(task, sig);
}

void signal_recompute_pending(struct task *task) {
    /* Recompute whether task has any deliverable pending signals */
    if (!task || !task->signal)
        return;

    /* This would update any cached "has_pending" flags */
    /* For now, pending signals are checked on demand */
}

void signal_wake_task(struct task *task, bool group_wide) {
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

bool signal_is_blocked(const struct task *task, int32_t sig) {
    if (!task || !task->signal)
        return false;

    /* Use 0-based indexing (sig - 1) */
    int idx = (sig - 1) / 64;
    int bit = (sig - 1) % 64;

    if (idx >= KERNEL_SIG_NUM_WORDS)
        return false;

    return (task->signal->blocked.sig[idx] & (1ULL << bit)) != 0;
}

bool signal_is_pending(const struct task *task, int32_t sig) {
    if (!task || !task->signal || sig < 1 || sig > KERNEL_SIG_NUM)
        return false;

    int idx = (sig - 1) / 64;
    int bit = (sig - 1) % 64;

    if (idx >= KERNEL_SIG_NUM_WORDS)
        return false;

    return ((task->thread_pending_signals | task->signal->shared_pending.sig[idx]) &
            (1ULL << bit)) != 0;
}

bool signal_has_unblocked_pending(const struct task *task) {
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

void signal_reset_on_exec(struct task *task) {
    if (!task || !task->signal)
        return;

    /* Reset signal handlers that have SA_RESETHAND flag set */
    /* For now, simplified: reset pending signals */
    sigemptyset(&task->signal->pending);
    sigemptyset(&task->signal->shared_pending);
    task->thread_pending_signals = 0;
}

int signal_init_task(struct task *task) {
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

int do_sigaction(int32_t sig, const struct sigaction *act, struct sigaction *oldact) {
    if (sig < 1 || sig >= KERNEL_SIG_NUM) {
        return -EINVAL;
    }

    if (sig == SIGKILL || sig == SIGSTOP) {
        return -EINVAL;
    }

    struct task *task = task_current();
    if (!task || !task->signal) {
        return -ESRCH;
    }

    if (oldact) {
        *oldact = task->signal->actions[sig - 1];
    }

    if (act) {
        task->signal->actions[sig - 1] = *act;
    }

    return 0;
}

int do_sigprocmask(int how, const sigset_t *set, sigset_t *oldset) {
	struct task *task = task_current();
	if (!task || !task->signal) {
		return -ESRCH;
	}

	struct signal_state *sig = task->signal;

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
			return -EINVAL;
		}
	}

	return 0;
}

int do_sigsetmask(const sigset_t *set, sigset_t *oldset) {
    return do_sigprocmask(SIG_SETMASK, set, oldset);
}

int do_sigpending(sigset_t *set) {
    if (!set) {
        return -EFAULT;
    }

    struct task *task = task_current();
    if (!task || !task->signal) {
        return -ESRCH;
    }

    memset(set, 0, sizeof(*set));
    set->sig[0] = task->thread_pending_signals;
    for (int i = 0; i < KERNEL_SIG_NUM_WORDS; i++) {
        set->sig[i] |= task->signal->shared_pending.sig[i];
    }
    return 0;
}

int do_signal(int32_t signum, __sighandler_t handler, __sighandler_t *old_handler) {
    if (signum < 1 || signum >= KERNEL_SIG_NUM) {
        return -EINVAL;
    }

    if (signum == SIGKILL || signum == SIGSTOP) {
        return -EINVAL;
    }

    struct task *task = task_current();
    if (!task || !task->signal) {
        return -ESRCH;
    }

    if (old_handler) {
        *old_handler = task->signal->actions[signum - 1].sa_handler;
    }
    task->signal->actions[signum - 1].sa_handler = handler;
    task->signal->actions[signum - 1].sa_flags = 0;
    task->signal->actions[signum - 1].sa_restorer = 0;
    sigemptyset(&task->signal->actions[signum - 1].sa_mask);

    return 0;
}

int do_raise(int32_t sig) {
    struct task *task = task_current();
    if (!task) {
        return -ESRCH;
    }
    return signal_generate_task(task, sig);
}

static int is_sigset_empty(sigset_t *set) {
    return sigisemptyset(set);
}

int do_pause(void) {
    struct task *task = task_current();
    if (!task) {
        return -ESRCH;
    }

    kernel_mutex_lock(&task->wait_lock);

    while (task->thread_pending_signals == 0 && is_sigset_empty(&task->signal->shared_pending)) {
        task->waiters++;
        kernel_cond_wait(&task->wait_cond, &task->wait_lock);
        task->waiters--;
    }

    kernel_mutex_unlock(&task->wait_lock);

    return -EINTR;
}

int do_sigsuspend(const sigset_t *mask) {
    struct task *task = task_current();
    if (!task || !task->signal) {
        return -ESRCH;
    }

    if (!mask) {
        return -EFAULT;
    }

    /* Save old mask */
    sigset_t old_mask = task->signal->blocked;

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

    return -EINTR;
}

int do_kill(int32_t pid, int32_t sig) {
    if (sig < 0 || sig > KERNEL_SIG_NUM) {
        return -EINVAL;
    }

    int result;

    if (pid <= 0) {
        /* Process group handling */
        if (pid == 0) {
            /* Current process group */
            struct task *task = task_current();
            if (!task) {
                return -ESRCH;
            }
            result = signal_generate_pgrp(task->pgid, sig);
        } else if (pid == -1) {
            /* All processes (privileged) */
            return -EPERM;
        } else {
            /* Process group |pid| */
            result = signal_generate_pgrp(-pid, sig);
        }
        return result;
    }

    struct task *task = task_lookup(pid);
    if (!task) {
        return -ESRCH;
    }

    result = signal_send_process(task, sig);
    task_put(task);
    return result;
}

int do_killpg(int32_t pgrp, int32_t sig) {
    return signal_generate_pgrp(pgrp, sig);
}

int do_sigaltstack(const stack_t *new_stack, stack_t *old_stack) {
    struct task *task = task_current();

    if (!task || !task->signal) {
        return -ESRCH;
    }
    if (old_stack) {
        *old_stack = task->signal->altstack;
    }
    if (new_stack) {
        if ((new_stack->ss_flags & ~(2 | (int32_t)(1U << 31))) != 0) {
            return -EINVAL;
        }
        if ((new_stack->ss_flags & 2) == 0 && new_stack->ss_size < 5120) {
            return -ENOMEM;
        }
        task->signal->altstack = *new_stack;
    }
    return 0;
}

int signal_prepare_frame_impl(struct task *task, int32_t sig, uint64_t return_pc,
                              uint64_t current_sp, uint64_t *frame_sp_out) {
    uint64_t frame_sp;
    uint64_t frame_record[frame_record_words];
    struct ucontext context;
    size_t frame_size = frame_ucontext_offset + sizeof(context);
    const struct sigaction *action;

    if (!task || !task->signal || !task->mm || sig < 1 || sig > KERNEL_SIG_NUM || !frame_sp_out) {
        return -EINVAL;
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
        return -EFAULT;
    }
    frame_sp -= frame_size;
    frame_sp &= ~15ULL;
    memset(frame_record, 0, sizeof(frame_record));
    frame_record[0] = (uint64_t)sig;
    frame_record[1] = return_pc;
    frame_record[2] = (uint64_t)(uintptr_t)action->sa_handler;
    frame_record[3] = (uint64_t)(uint32_t)action->sa_flags;
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
    task->mm->signal_handler_pc = (uint64_t)(uintptr_t)action->sa_handler;
    task->mm->signal_frame_flags = (uint64_t)(uint32_t)action->sa_flags;
    task->mm->signal_frame_restorer_pc = (uint64_t)(uintptr_t)action->sa_restorer;
    task->mm->signal_frame_mask = task->signal->blocked.sig[0];
    task->mm->signal_frame_altstack_sp = (uint64_t)(uintptr_t)task->signal->altstack.ss_sp;
    task->mm->signal_frame_altstack_size = (uint64_t)task->signal->altstack.ss_size;
    task->mm->signal_frame_altstack_flags = (uint64_t)(uint32_t)task->signal->altstack.ss_flags;
    task->mm->signal_frame_current_sp = current_sp;
    task->mm->signal_frame_size = frame_size;
    task->mm->signal_frame_ucontext_flags = 1;
    task->mm->signal_frame_restartable = (action->sa_flags & SA_RESTART) != 0 ? 1 : 0;
    task->mm->signal_frame_restart_return_pc = return_pc;
    task->mm->signal_frame_restart_sp = current_sp;
    task->mm->signal_frame_restart_signo = (uint64_t)sig;
    for (int i = 0; i < KERNEL_SIG_NUM_WORDS; i++) {
        task->signal->blocked.sig[i] |= action->sa_mask.sig[i];
    }
    if ((action->sa_flags & SA_NODEFER) == 0) {
        task->signal->blocked.sig[(sig - 1) >> 6] |= (1ULL << ((sig - 1) & 63));
    }
    if ((action->sa_flags & SA_RESETHAND) != 0 && sig != SIGKILL && sig != SIGSTOP) {
        task->signal->actions[sig - 1].sa_handler = SIG_DFL;
        task->signal->actions[sig - 1].sa_flags = 0;
        task->signal->actions[sig - 1].sa_restorer = 0;
        sigemptyset(&task->signal->actions[sig - 1].sa_mask);
    }
    *frame_sp_out = frame_sp;
    return 0;
}

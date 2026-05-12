#ifndef PRIVATE_KERNEL_SIGNAL_STATE_H
#define PRIVATE_KERNEL_SIGNAL_STATE_H

#include <linux/atomic.h>
#include <linux/types.h>

#include "kernel/signal.h"
#include "internal/mutex.h"

#ifdef __cplusplus
extern "C" {
#endif

struct signal_queue_entry {
    int32_t sig;
    int32_t si_signo;
    int32_t si_errno;
    int32_t si_code;
    u64 fault_addr;
    struct signal_queue_entry *next;
};

struct signal_queue {
    struct signal_queue_entry *head;
    struct signal_queue_entry *tail;
    int count;
    kernel_mutex_t lock;
};

struct signal_altstack {
    void *ss_sp;
    size_t ss_size;
    int32_t ss_flags;
};

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

struct pending_signals {
    struct signal_mask_bits signal;
    struct signal_queue queue;
};

#ifdef __cplusplus
}
#endif

#endif

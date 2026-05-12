#ifndef PRIVATE_KERNEL_SIGNAL_STATE_H
#define PRIVATE_KERNEL_SIGNAL_STATE_H

#include <linux/atomic.h>
#include <linux/signal.h>
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

struct signal_state {
    atomic_t refs;
    struct sigaction actions[KERNEL_SIG_NUM];
    sigset_t blocked;
    sigset_t pending;
    sigset_t shared_pending;
    struct signal_queue queue;
    stack_t altstack;
    kernel_mutex_t lock;
};

struct pending_signals {
    sigset_t signal;
    struct signal_queue queue;
};

#ifdef __cplusplus
}
#endif

#endif

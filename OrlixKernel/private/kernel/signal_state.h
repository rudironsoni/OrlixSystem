#ifndef PRIVATE_KERNEL_SIGNAL_STATE_H
#define PRIVATE_KERNEL_SIGNAL_STATE_H

#include <linux/atomic.h>
#include <linux/signal.h>
#include <linux/types.h>

#include "kernel/signal.h"
#include "private/kernel/mutex_state.h"

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

int signal_frame_restart_kind_get_task(const struct task *task,
                                       uint64_t *kind_out);
int signal_frame_restart_record_task(struct task *task,
                                     uint64_t kind,
                                     uint64_t arg0,
                                     uint64_t arg1,
                                     uint64_t arg2,
                                     uint64_t arg3,
                                     uint64_t arg4,
                                     uint64_t arg5);

#ifdef __cplusplus
}
#endif

#endif

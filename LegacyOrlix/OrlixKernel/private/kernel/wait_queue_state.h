#ifndef PRIVATE_KERNEL_WAIT_QUEUE_STATE_H
#define PRIVATE_KERNEL_WAIT_QUEUE_STATE_H

#include "kernel/wait_queue.h"
#include "private/kernel/mutex_state.h"

#ifdef __cplusplus
extern "C" {
#endif

struct wait_queue_head {
    kernel_mutex_t lock;
    kernel_cond_t cond;
};

#ifdef __cplusplus
}
#endif

#endif

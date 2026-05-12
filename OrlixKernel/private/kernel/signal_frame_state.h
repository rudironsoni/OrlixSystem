#ifndef PRIVATE_KERNEL_SIGNAL_FRAME_STATE_H
#define PRIVATE_KERNEL_SIGNAL_FRAME_STATE_H

#include "kernel/task.h"

#ifdef __cplusplus
extern "C" {
#endif

struct signal_frame_state {
    uint64_t sp;
    uint64_t signo;
    uint64_t return_pc;
    uint64_t handler_pc;
    uint64_t flags;
    uint64_t restorer_pc;
    uint64_t mask;
    uint64_t altstack_sp;
    uint64_t altstack_size;
    uint64_t altstack_flags;
    uint64_t current_sp;
    uint64_t size;
    uint64_t ucontext_flags;
    uint64_t restartable;
    uint64_t restart_return_pc;
    uint64_t restart_sp;
    uint64_t restart_signo;
    enum task_restart_kind restart_kind;
    uint64_t restart_arg0;
    uint64_t restart_arg1;
    uint64_t restart_arg2;
    uint64_t restart_arg3;
    uint64_t restart_arg4;
    uint64_t restart_arg5;
};

#ifdef __cplusplus
}
#endif

#endif

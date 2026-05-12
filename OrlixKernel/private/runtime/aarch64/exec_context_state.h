#ifndef PRIVATE_RUNTIME_AARCH64_EXEC_CONTEXT_STATE_H
#define PRIVATE_RUNTIME_AARCH64_EXEC_CONTEXT_STATE_H

#include "runtime/aarch64/exec_context.h"

#ifdef __cplusplus
extern "C" {
#endif

struct aarch64_exec_context {
    u64 pc;
    u64 sp;
    u64 steps;
    u32 stopped;
    struct task *task;
    long (*read_memory)(struct task *task, u64 addr, void *buf, size_t count);
    long (*write_memory)(struct task *task, u64 addr, const void *buf, size_t count);
};

#ifdef __cplusplus
}
#endif

#endif

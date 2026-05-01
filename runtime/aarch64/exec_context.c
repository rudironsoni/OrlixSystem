/* IXLandSystem/runtime/aarch64/exec_context.c
 * Virtual aarch64 execution context setup from task exec handoff state.
 */

#include "exec_context.h"

#include <errno.h>

#include "../../kernel/task.h"

#define AARCH64_INSN_NOP 0xd503201fU
#define AARCH64_INSN_BRK_BASE 0xd4200000U
#define AARCH64_INSN_BRK_MASK 0xffe0001fU

int aarch64_exec_context_from_task(struct task_struct *task, struct aarch64_exec_context *context) {
    const struct task_exec_handoff *handoff;

    if (!task || !context) {
        errno = EFAULT;
        return -1;
    }

    handoff = task_get_exec_handoff_impl(task);
    if (!handoff ||
        handoff->aarch64_pc == 0 ||
        handoff->aarch64_sp == 0 ||
        !handoff->read_memory ||
        !handoff->write_memory) {
        errno = ENOEXEC;
        return -1;
    }

    context->pc = handoff->aarch64_pc;
    context->sp = handoff->aarch64_sp;
    context->steps = 0;
    context->stopped = 0;
    context->task = task;
    context->read_memory = handoff->read_memory;
    context->write_memory = handoff->write_memory;
    return 0;
}

int aarch64_exec_context_step(struct aarch64_exec_context *context) {
    uint32_t insn = 0;

    if (!context || !context->task || !context->read_memory) {
        errno = EFAULT;
        return -1;
    }
    if (context->stopped) {
        return 0;
    }
    if (context->read_memory(context->task, context->pc, &insn, sizeof(insn)) != (long)sizeof(insn)) {
        return -1;
    }

    if (insn == AARCH64_INSN_NOP) {
        context->pc += sizeof(insn);
        context->steps++;
        return 1;
    }
    if ((insn & AARCH64_INSN_BRK_MASK) == AARCH64_INSN_BRK_BASE) {
        context->stopped = 1;
        context->steps++;
        return 0;
    }

    errno = ENOSYS;
    return -1;
}

int aarch64_exec_context_run(struct aarch64_exec_context *context, uint64_t max_steps, uint64_t *out_steps) {
    uint64_t ran = 0;

    if (!context) {
        errno = EFAULT;
        return -1;
    }
    while (ran < max_steps && !context->stopped) {
        int ret = aarch64_exec_context_step(context);
        if (ret < 0) {
            return -1;
        }
        ran++;
        if (ret == 0) {
            break;
        }
    }
    if (out_steps) {
        *out_steps = ran;
    }
    return 0;
}

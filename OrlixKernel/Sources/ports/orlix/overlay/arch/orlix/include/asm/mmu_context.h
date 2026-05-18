/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _ASM_ORLIX_MMU_CONTEXT_H
#define _ASM_ORLIX_MMU_CONTEXT_H

#include <asm-generic/mm_hooks.h>

struct mm_struct;
struct task_struct;

static inline void switch_mm(struct mm_struct *prev, struct mm_struct *next,
				     struct task_struct *tsk)
{
}

#include <asm-generic/mmu_context.h>

#endif /* _ASM_ORLIX_MMU_CONTEXT_H */

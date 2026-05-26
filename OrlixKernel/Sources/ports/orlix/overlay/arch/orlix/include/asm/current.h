/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _ASM_ORLIX_CURRENT_H
#define _ASM_ORLIX_CURRENT_H

#include <linux/compiler.h>

#ifndef __ASSEMBLY__

struct task_struct;

extern struct task_struct *orlix_current_task;

static __always_inline struct task_struct *get_current(void)
{
	return orlix_current_task;
}

#define current get_current()

#endif /* !__ASSEMBLY__ */

#endif /* _ASM_ORLIX_CURRENT_H */

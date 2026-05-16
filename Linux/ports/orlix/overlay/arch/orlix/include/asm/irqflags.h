/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _ASM_ORLIX_IRQFLAGS_H
#define _ASM_ORLIX_IRQFLAGS_H

#include <linux/compiler.h>

#define ARCH_IRQ_DISABLED	0
#define ARCH_IRQ_ENABLED	1

extern unsigned long orlix_irq_flags;

#define arch_local_save_flags arch_local_save_flags
static __always_inline unsigned long arch_local_save_flags(void)
{
	return READ_ONCE(orlix_irq_flags);
}

#define arch_local_irq_restore arch_local_irq_restore
static __always_inline void arch_local_irq_restore(unsigned long flags)
{
	WRITE_ONCE(orlix_irq_flags, flags ? ARCH_IRQ_ENABLED : ARCH_IRQ_DISABLED);
	barrier();
}

#define arch_local_irq_enable arch_local_irq_enable
static __always_inline void arch_local_irq_enable(void)
{
	arch_local_irq_restore(ARCH_IRQ_ENABLED);
}

#define arch_local_irq_disable arch_local_irq_disable
static __always_inline void arch_local_irq_disable(void)
{
	arch_local_irq_restore(ARCH_IRQ_DISABLED);
}

#include <asm-generic/irqflags.h>

#endif /* _ASM_ORLIX_IRQFLAGS_H */

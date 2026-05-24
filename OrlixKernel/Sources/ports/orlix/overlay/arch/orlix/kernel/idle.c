// SPDX-License-Identifier: GPL-2.0-only

#include <linux/irqflags.h>
#include <internal/asm/host_thread.h>

void arch_cpu_idle(void)
{
	raw_local_irq_enable();
	orlix_host_thread_idle();
	raw_local_irq_disable();
}

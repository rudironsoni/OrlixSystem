// SPDX-License-Identifier: GPL-2.0-only

#include <linux/irqflags.h>
#include <asm/time.h>
#include <internal/asm/host_thread.h>

void arch_cpu_idle(void)
{
	raw_local_irq_enable();
	orlix_timer_poll();
	orlix_host_thread_idle_until(orlix_timer_next_deadline_ns());
	orlix_timer_poll();
	raw_local_irq_disable();
}

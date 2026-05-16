// SPDX-License-Identifier: GPL-2.0-only

#include <linux/irqflags.h>
#include <linux/printk.h>
#include <asm/processor.h>

static void orlix_halt_loop(const char *reason)
{
	local_irq_disable();
	pr_emerg("Orlix %s requested before host lifecycle support exists\n",
		 reason);
	for (;;)
		cpu_relax();
}

void machine_restart(char *cmd)
{
	(void)cmd;
	orlix_halt_loop("restart");
}

void machine_halt(void)
{
	orlix_halt_loop("halt");
}

void machine_power_off(void)
{
	orlix_halt_loop("power-off");
}

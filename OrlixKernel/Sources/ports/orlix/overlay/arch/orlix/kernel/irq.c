// SPDX-License-Identifier: GPL-2.0-only

#include <linux/compiler.h>
#include <linux/init.h>
#include <linux/irqchip.h>

unsigned long orlix_irq_flags = 1;

void __init init_IRQ(void)
{
	irqchip_init();
}

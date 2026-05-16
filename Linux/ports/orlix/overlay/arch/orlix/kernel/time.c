// SPDX-License-Identifier: GPL-2.0-only

#include <linux/clocksource.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/of_clk.h>

void __init time_init(void)
{
	of_clk_init(NULL);
	timer_probe();
	lpj_fine = 1000000UL;
}

#include <linux/init.h>
#include <linux/printk.h>

void __init setup_arch(char **cmdline_p)
{
	(void)cmdline_p;
	pr_info("Orlix architecture setup\n");
}

// SPDX-License-Identifier: GPL-2.0

#include <linux/init.h>
#include <linux/module.h>
#include <linux/printk.h>

static int __init orlix_tty_console_init(void)
{
	pr_info("orlix tty console driver skeleton registered\n");
	return 0;
}

static void __exit orlix_tty_console_exit(void)
{
	pr_info("orlix tty console driver skeleton unregistered\n");
}

module_init(orlix_tty_console_init);
module_exit(orlix_tty_console_exit);

MODULE_LICENSE("GPL");

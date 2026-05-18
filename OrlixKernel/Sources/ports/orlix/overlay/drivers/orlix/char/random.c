// SPDX-License-Identifier: GPL-2.0

#include <linux/init.h>
#include <linux/module.h>
#include <linux/printk.h>

static int __init orlix_char_random_init(void)
{
	pr_info("orlix char random driver skeleton registered\n");
	return 0;
}

static void __exit orlix_char_random_exit(void)
{
	pr_info("orlix char random driver skeleton unregistered\n");
}

module_init(orlix_char_random_init);
module_exit(orlix_char_random_exit);

MODULE_LICENSE("GPL");

// SPDX-License-Identifier: GPL-2.0

#include <linux/init.h>
#include <linux/module.h>
#include <linux/printk.h>

static int __init orlix_block_file_init(void)
{
	pr_info("orlix block file driver skeleton registered\n");
	return 0;
}

static void __exit orlix_block_file_exit(void)
{
	pr_info("orlix block file driver skeleton unregistered\n");
}

module_init(orlix_block_file_init);
module_exit(orlix_block_file_exit);

MODULE_LICENSE("GPL");

// SPDX-License-Identifier: GPL-2.0

#include <linux/init.h>
#include <linux/module.h>
#include <linux/printk.h>

static int __init orlix_block_image_init(void)
{
	pr_info("orlix block image driver skeleton registered\n");
	return 0;
}

static void __exit orlix_block_image_exit(void)
{
	pr_info("orlix block image driver skeleton unregistered\n");
}

module_init(orlix_block_image_init);
module_exit(orlix_block_image_exit);

MODULE_LICENSE("GPL");

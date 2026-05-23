// SPDX-License-Identifier: GPL-2.0-only

#include <linux/seq_file.h>

static void *orlix_cpuinfo_start(struct seq_file *m, loff_t *pos)
{
	(void)m;
	return *pos == 0 ? (void *)1 : NULL;
}

static void *orlix_cpuinfo_next(struct seq_file *m, void *v, loff_t *pos)
{
	(void)m;
	(void)v;
	++*pos;
	return NULL;
}

static void orlix_cpuinfo_stop(struct seq_file *m, void *v)
{
	(void)m;
	(void)v;
}

static int orlix_cpuinfo_show(struct seq_file *m, void *v)
{
	(void)v;
	seq_puts(m, "processor\t: 0\n");
	seq_puts(m, "architecture\t: orlix\n");
	return 0;
}

const struct seq_operations cpuinfo_op = {
	.start = orlix_cpuinfo_start,
	.next = orlix_cpuinfo_next,
	.stop = orlix_cpuinfo_stop,
	.show = orlix_cpuinfo_show,
};

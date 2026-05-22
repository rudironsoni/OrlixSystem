// SPDX-License-Identifier: GPL-2.0-only

#include <linux/compiler.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/irqdesc.h>
#include <linux/irqdomain.h>
#include <linux/of.h>

unsigned long orlix_irq_flags = 1;

static struct irq_domain *orlix_irq_domain;

static int orlix_irq_domain_map(struct irq_domain *domain,
				unsigned int virq,
				irq_hw_number_t hwirq)
{
	(void)domain;
	(void)hwirq;

	irq_set_chip_and_handler(virq, &dummy_irq_chip, handle_simple_irq);
	return 0;
}

static const struct irq_domain_ops orlix_irq_domain_ops = {
	.map = orlix_irq_domain_map,
	.xlate = irq_domain_xlate_onecell,
};

void __init init_IRQ(void)
{
	struct device_node *node;

	node = of_find_compatible_node(NULL, NULL, "orlix,host-intc");
	if (!node)
		return;

	orlix_irq_domain = irq_domain_add_linear(node, 64,
						 &orlix_irq_domain_ops, NULL);
	of_node_put(node);
}

int orlix_irq_dispatch(unsigned int hwirq)
{
	if (!orlix_irq_domain)
		return -ENODEV;

	return generic_handle_domain_irq(orlix_irq_domain, hwirq);
}

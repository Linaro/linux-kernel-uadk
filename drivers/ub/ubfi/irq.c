// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) HiSilicon Technologies Co., Ltd. 2025. All rights reserved.
 */

#include <linux/acpi_iort.h>
#include <linux/irqdomain.h>
#include <linux/msi.h>
#include <linux/platform_device.h>

int ubrt_register_gsi(u32 hwirq, int trigger, int polarity, const char *name,
		      struct resource *res)
{
#ifdef CONFIG_ACPI
	int irq;

	if (!res) {
		pr_err("ub register gsi, res is null\n");
		return -EINVAL;
	}

	irq = acpi_register_gsi(NULL, hwirq, trigger, polarity);
	if (irq <= 0) {
		pr_err("could not register ub gsi hwirq %u\n", hwirq);
		return -EINVAL;
	}

	res->name = name;
	res->start = irq;
	res->end = irq;
	res->flags = IORESOURCE_IRQ;
#endif
	return 0;
}
EXPORT_SYMBOL_GPL(ubrt_register_gsi);

void ubrt_unregister_gsi(u32 hwirq)
{
#ifdef CONFIG_ACPI
	acpi_unregister_gsi(hwirq);
#endif
}
EXPORT_SYMBOL_GPL(ubrt_unregister_gsi);

// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) HiSilicon Technologies Co., Ltd. 2025. All rights reserved.
 */

#include <linux/device.h>

#include "ubus_inner.h"

struct bus_type ub_bus_type = {
	.name = "ub",
};
EXPORT_SYMBOL_GPL(ub_bus_type);

int __ub_register_driver(struct ub_driver *drv, struct module *owner,
			 const char *mod_name)
{
	if (!drv)
		return -EINVAL;

	drv->driver.name = drv->name;
	drv->driver.bus = &ub_bus_type;
	drv->driver.owner = owner;
	drv->driver.mod_name = mod_name;

	return driver_register(&drv->driver);
}
EXPORT_SYMBOL_GPL(__ub_register_driver);

void ub_unregister_driver(struct ub_driver *drv)
{
	if (!drv)
		return;

	driver_unregister(&drv->driver);
}
EXPORT_SYMBOL_GPL(ub_unregister_driver);

static int __init ub_driver_init(void)
{
	return bus_register(&ub_bus_type);
}
postcore_initcall(ub_driver_init);

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

struct ub_dynid {
	struct list_head node;
	struct ub_device_id id;
};

static void ub_free_dynids(struct ub_driver *drv)
{
	struct ub_dynid *dynid, *n;

	spin_lock(&drv->dynids.lock);
	list_for_each_entry_safe(dynid, n, &drv->dynids.list, node) {
		list_del(&dynid->node);
		kfree(dynid);
	}
	spin_unlock(&drv->dynids.lock);
}

int __ub_register_driver(struct ub_driver *drv, struct module *owner,
			 const char *mod_name)
{
	if (!drv)
		return -EINVAL;

	drv->driver.name = drv->name;
	drv->driver.bus = &ub_bus_type;
	drv->driver.owner = owner;
	drv->driver.mod_name = mod_name;
	drv->driver.groups = drv->groups;

	spin_lock_init(&drv->dynids.lock);
	INIT_LIST_HEAD(&drv->dynids.list);

	return driver_register(&drv->driver);
}
EXPORT_SYMBOL_GPL(__ub_register_driver);

void ub_unregister_driver(struct ub_driver *drv)
{
	if (!drv)
		return;

	driver_unregister(&drv->driver);
	ub_free_dynids(drv);
}
EXPORT_SYMBOL_GPL(ub_unregister_driver);

static int __init ub_driver_init(void)
{
	return bus_register(&ub_bus_type);
}
postcore_initcall(ub_driver_init);

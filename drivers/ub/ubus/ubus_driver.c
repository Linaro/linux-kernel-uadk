// SPDX-License-Identifier: GPL-2.0+
/*
 * (C) Copyright 2002-2004, 2007 Greg Kroah-Hartman <greg@kroah.com>
 * (C) Copyright 2007 Novell Inc.
 * Copyright (c) HiSilicon Technologies Co., Ltd. 2025. All rights reserved.
 *
 * Thanks to Greg Kroah-Hartman and Novell Inc. for their previous work.
 *
 */

#define pr_fmt(fmt)	"ubus driver: " fmt

#include <linux/module.h>
#include <linux/pm_runtime.h>

#include "ubus.h"

struct ub_entity *ub_entity_get(struct ub_entity *dev)
{
	if (dev)
		get_device(&dev->dev);
	return dev;
}
EXPORT_SYMBOL_GPL(ub_entity_get);

void ub_entity_put(struct ub_entity *dev)
{
	if (dev)
		put_device(&dev->dev);
}
EXPORT_SYMBOL_GPL(ub_entity_put);

static const struct ub_device_id ub_entity_id_any = {
	.vendor = (__u32)UB_ANY_ID,
	.device = (__u32)UB_ANY_ID,
	.mod_vendor = (__u32)UB_ANY_ID,
	.module = (__u32)UB_ANY_ID,
};

static inline const struct ub_device_id *
ub_match_one_device(const struct ub_device_id *id, const struct ub_entity *dev)
{
	if ((id->vendor == UB_ANY_ID || id->vendor == uent_vendor(dev)) &&
	    (id->device == UB_ANY_ID || id->device == uent_device(dev)) &&
	    (id->mod_vendor == UB_ANY_ID || id->mod_vendor == dev->mod_vendor) &&
	    (id->module == UB_ANY_ID || id->module == dev->module) &&
	    !((id->class_code ^ uent_class(dev)) & id->class_mask))
		return id;
	return NULL;
}

const struct ub_device_id *ub_match_id(const struct ub_device_id *ids,
				       struct ub_entity *dev)
{
	if (ids && dev) {
		while (ids->vendor || ids->mod_vendor || ids->class_mask) {
			if (ub_match_one_device(ids, dev))
				return ids;
			ids++;
		}
	}
	return NULL;
}

static const struct ub_device_id *ub_match_device(struct ub_driver *drv,
						  struct ub_entity *dev)
{
	const struct ub_device_id *found_id = NULL, *ids;

	/* When driver_override is set, only bind to the matching driver */
	if (dev->driver_override && strcmp(dev->driver_override, drv->name))
		return NULL;

	for (ids = drv->id_table; (found_id = ub_match_id(ids, dev));
	     ids = found_id + 1) {
		if (found_id->override_only) {
			if (dev->driver_override)
				return found_id;
		} else {
			return found_id;
		}
	}

	/* driver_override will always match, send a dummy id */
	if (dev->driver_override)
		return &ub_entity_id_any;

	return NULL;
}

static int ub_bus_match(struct device *dev, struct device_driver *drv)
{
	struct ub_driver *ub_drv = to_ub_driver(drv);
	struct ub_entity *ub_entity = to_ub_entity(dev);
	const struct ub_device_id *found_id;

	if (!ub_entity->match_driver)
		return 0;

	found_id = ub_match_device(ub_drv, ub_entity);
	if (found_id)
		return 1;

	return 0;
}

static int ub_call_probe(struct ub_driver *drv, struct ub_entity *dev,
			 const struct ub_device_id *id)
{
	int ret;

	dev->driver = drv;
	/*
	 * Probe function should return < 0 for failure, 0 for success
	 * Treat values > 0 as success, but warn.
	 */
	ret = drv->probe(dev, id);
	if (ret < 0) {
		dev->driver = NULL;
		return ret;
	} else if (ret > 0) {
		ub_warn(dev, "Driver probe function unexpectedly, ret=%d\n",
			ret);
	}

	return 0;
}

static int __ub_entity_probe(struct ub_driver *drv, struct ub_entity *dev)
{
	const struct ub_device_id *id;
	int ret = 0;

	if (drv->probe) {
		ret = -ENODEV;

		id = ub_match_device(drv, dev);
		if (id)
			ret = ub_call_probe(drv, dev, id);
	}

	return ret;
}

static int ub_entity_probe(struct device *dev)
{
	struct ub_driver *drv = to_ub_driver(dev->driver);
	struct ub_entity *ub_entity = to_ub_entity(dev);
	int ret;

	ub_entity_get(ub_entity);
	ret = __ub_entity_probe(drv, ub_entity);
	if (ret)
		ub_entity_put(ub_entity);
	return ret;
}

static void ub_entity_remove(struct device *dev)
{
	struct ub_entity *ub_entity = to_ub_entity(dev);
	struct ub_driver *drv = ub_entity->driver;

	if (drv->remove)
		drv->remove(ub_entity);

	ub_entity->driver = NULL;

	ub_entity_put(ub_entity);
}

static void ub_entity_shutdown(struct device *dev)
{
	struct ub_entity *uent = to_ub_entity(dev);
	struct ub_driver *drv = uent->driver;

	ub_dbg(uent, "come shutdown\n");

	pm_runtime_resume(dev);

	if (drv && drv->shutdown)
		drv->shutdown(uent);
}

static int ub_uevent(const struct device *dev, struct kobj_uevent_env *env)
{
	struct ub_entity *uent;

	if (!dev)
		return -ENODEV;

	uent = to_ub_entity(dev);

	if (add_uevent_var(env, "UB_ID=%04X:%04X", uent_vendor(uent),
			   uent_device(uent)))
		return -ENOMEM;

	if (add_uevent_var(env, "UB_MODULE=%04X:%04X", uent->mod_vendor,
			   uent->module))
		return -ENOMEM;

	if (add_uevent_var(env, "UB_TYPE=%01X", uent_type(uent)))
		return -ENOMEM;

	if (add_uevent_var(env, "UB_CLASS=%04X", uent_class(uent)))
		return -ENOMEM;

	if (add_uevent_var(env, "UB_VERSION=%01X", uent_version(uent)))
		return -ENOMEM;

	if (add_uevent_var(env, "UB_SEQ_NUM=%016llX", uent_seq(uent)))
		return -ENOMEM;

	if (add_uevent_var(env, "UB_ENTITY_NAME=%s", ub_name(uent)))
		return -ENOMEM;

	if (add_uevent_var(env, "MODALIAS=ub:v%04Xd%04Xmv%04Xm%04Xc%04X",
			   uent_vendor(uent), uent_device(uent),
			   uent->mod_vendor, uent->module, uent_class(uent)))
		return -ENOMEM;

	return 0;
}

void ub_bus_type_init(void)
{
	ub_bus_type.match = ub_bus_match;
	ub_bus_type.uevent = ub_uevent;
	ub_bus_type.probe = ub_entity_probe;
	ub_bus_type.remove = ub_entity_remove;
	ub_bus_type.shutdown = ub_entity_shutdown;
}

void ub_bus_type_uninit(void)
{
	ub_bus_type.match = NULL;
	ub_bus_type.uevent = NULL;
	ub_bus_type.probe = NULL;
	ub_bus_type.remove = NULL;
	ub_bus_type.shutdown = NULL;
}

int ub_host_probe(void)
{
	ub_bus_type_init();
	return 0;
}
EXPORT_SYMBOL_GPL(ub_host_probe);

void ub_host_remove(void)
{
	ub_bus_type_uninit();
}
EXPORT_SYMBOL_GPL(ub_host_remove);

static int __init ubus_driver_init(void)
{
	pr_info("Ubus driver init successfully.\n");
	return 0;
}

static void __exit ubus_driver_exit(void)
{
	pr_info("Ubus driver exit successfully.\n");
}
module_init(ubus_driver_init);
module_exit(ubus_driver_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("UB bus driver");
MODULE_IMPORT_NS(UB_UBFI);
MODULE_IMPORT_NS(UB_UBUS);

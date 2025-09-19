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

#include "sysfs.h"
#include "ubus.h"
#include "ubus_controller.h"

static DEFINE_MUTEX(manage_subsystem_ops_mutex);
static const struct ub_manage_subsystem_ops *manage_subsystem_ops;

int register_ub_manage_subsystem_ops(const struct ub_manage_subsystem_ops *ops)
{
	if (!ops)
		return -EINVAL;

	mutex_lock(&manage_subsystem_ops_mutex);
	if (!manage_subsystem_ops) {
		manage_subsystem_ops = ops;
		mutex_unlock(&manage_subsystem_ops_mutex);
		pr_info("ub manage subsystem ops register successfully\n");
		return 0;
	}

	pr_warn("ub manage subsystem ops has been registered\n");
	mutex_unlock(&manage_subsystem_ops_mutex);

	return -EINVAL;
}
EXPORT_SYMBOL_GPL(register_ub_manage_subsystem_ops);

void unregister_ub_manage_subsystem_ops(const struct ub_manage_subsystem_ops *ops)
{
	if (!ops)
		return;

	mutex_lock(&manage_subsystem_ops_mutex);
	if (manage_subsystem_ops == ops) {
		manage_subsystem_ops = NULL;
		pr_info("ub manage subsystem ops unregister successfully\n");
	} else {
		pr_warn("ub manage subsystem ops is not registered by this vendor\n");
	}
	mutex_unlock(&manage_subsystem_ops_mutex);
}
EXPORT_SYMBOL_GPL(unregister_ub_manage_subsystem_ops);

const struct ub_manage_subsystem_ops *get_ub_manage_subsystem_ops(void)
{
	return manage_subsystem_ops;
}

struct ub_bus_controller *ub_find_bus_controller(u32 ctl_no)
{
	struct ub_bus_controller *ubc;

	list_for_each_entry(ubc, &ubc_list, node)
		if (ubc->ctl_no == ctl_no)
			return ubc;

	return NULL;
}
EXPORT_SYMBOL_GPL(ub_find_bus_controller);

int ub_get_bus_controller(struct ub_entity *ubc_dev[], unsigned int max_num,
		      unsigned int *real_num)
{
	struct ub_bus_controller *ubc;
	unsigned int ubc_num = 0;

	if (!real_num || !ubc_dev) {
		pr_err("%s: input parameters invalid\n", __func__);
		return -EINVAL;
	}

	list_for_each_entry(ubc, &ubc_list, node) {
		if (ubc_num >= max_num) {
			pr_err("ubc list num over max num %u\n", max_num);
			ub_put_bus_controller(ubc_dev, max_num);
			return -ENOMEM;
		}

		ubc_dev[ubc_num] = ub_entity_get(ubc->uent);
		ubc_num++;
	}
	*real_num = ubc_num;

	return 0;
}
EXPORT_SYMBOL_GPL(ub_get_bus_controller);

void ub_put_bus_controller(struct ub_entity *ubc_dev[], unsigned int num)
{
	unsigned int i;

	if (ubc_dev) {
		for (i = 0; i < num; i++)
			ub_entity_put(ubc_dev[i]);
		memset(ubc_dev, 0, sizeof(struct ub_entity *) * num);
	}
}
EXPORT_SYMBOL_GPL(ub_put_bus_controller);

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

struct ub_dynid {
	struct list_head node;
	struct ub_device_id id;
};

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
	struct ub_dynid *dynid;

	/* When driver_override is set, only bind to the matching driver */
	if (dev->driver_override && strcmp(dev->driver_override, drv->name))
		return NULL;

	/* Look at the dynamic ids first, before the static ones */
	spin_lock(&drv->dynids.lock);
	list_for_each_entry(dynid, &drv->dynids.list, node) {
		if (ub_match_one_device(&dynid->id, dev)) {
			found_id = &dynid->id;
			break;
		}
	}
	spin_unlock(&drv->dynids.lock);

	if (found_id)
		return found_id;

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

static int ub_add_dynid(struct ub_driver *drv,
			unsigned int vendor, unsigned int device,
			unsigned int mod_vendor, unsigned int module,
			unsigned int class_code, unsigned int class_mask,
			unsigned long driver_data)
{
	struct ub_dynid *dynid;

	dynid = kzalloc(sizeof(*dynid), GFP_KERNEL);
	if (!dynid)
		return -ENOMEM;

	dynid->id.vendor = vendor;
	dynid->id.device = device;
	dynid->id.mod_vendor = mod_vendor;
	dynid->id.module = module;
	dynid->id.class_code = class_code;
	dynid->id.class_mask = class_mask;
	dynid->id.driver_data = driver_data;

	spin_lock(&drv->dynids.lock);
	list_add_tail(&dynid->node, &drv->dynids.list);
	spin_unlock(&drv->dynids.lock);

	return driver_attach(&drv->driver);
}

static int ub_check_id_exist(struct ub_driver *udrv, u32 vendor, u32 device,
			     u32 mod_vendor, u32 module, u32 class_code)
{
	struct ub_entity *uent;
	int ret = 0;

	uent = kzalloc(sizeof(struct ub_entity), GFP_KERNEL);
	if (!uent)
		return -ENOMEM;

	uent_vendor(uent) = vendor;
	uent_device(uent) = device;
	uent_class(uent) = class_code;
	uent->mod_vendor = mod_vendor;
	uent->module = module;

	if (ub_match_device(udrv, uent))
		ret = -EEXIST;

	kfree(uent);

	return ret;
}

static int ub_check_driver_data_valid(const struct ub_device_id *ids,
				      unsigned long driver_data)
{
	/* Only accept driver_data values that match an exiting id_table entry */
	while (ids->vendor || ids->mod_vendor || ids->class_mask) {
		if (driver_data == ids->driver_data)
			return 0;
		ids++;
	}

	return -EINVAL;
}

static ssize_t new_id_store(struct device_driver *driver, const char *buf,
			    size_t count)
{
	u32 vendor, device, class_code = 0, class_mask = 0;
	u32 mod_vendor = UB_ANY_ID, module = UB_ANY_ID;
	struct ub_driver *udrv = to_ub_driver(driver);
	unsigned long driver_data = 0;
	int fileds, ret;

	fileds = sscanf(buf, "%x %x %x %x %x %x %lx", &vendor, &device,
			&mod_vendor, &module,
			&class_code, &class_mask, &driver_data);
	if (fileds < 2) /* 2 parameter for vender & device id */
		return -EINVAL;

	if (fileds != 7) { /* 7 for driver_data input */
		ret = ub_check_id_exist(udrv, vendor, device, mod_vendor,
					module, class_code);
		if (ret)
			return ret;
	}

	if (udrv->id_table) {
		ret = ub_check_driver_data_valid(udrv->id_table, driver_data);
		if (ret)
			return ret;
	}

	ret = ub_add_dynid(udrv, vendor, device, mod_vendor, module,
			   class_code, class_mask, driver_data);
	if (ret)
		return ret;

	return count;
}
static DRIVER_ATTR_WO(new_id);

static ssize_t remove_id_store(struct device_driver *driver, const char *buf,
			       size_t count)
{
	u32 vendor, device, class_code = 0, class_mask = 0;
	u32 mod_vendor = UB_ANY_ID, module = UB_ANY_ID;
	struct ub_driver *udrv = to_ub_driver(driver);
	struct ub_dynid *dynid, *n;
	ssize_t ret = -ENODEV;
	int fields;

	fields = sscanf(buf, "%x %x %x %x %x %x", &vendor, &device, &mod_vendor,
			&module, &class_code, &class_mask);
	if (fields < 2) /* 2 parameter for vender & device id */
		return -EINVAL;

	spin_lock(&udrv->dynids.lock);
	list_for_each_entry_safe(dynid, n, &udrv->dynids.list, node) {
		struct ub_device_id *id = &dynid->id;

		if ((id->vendor == vendor) &&
		    (id->device == device) &&
		    (mod_vendor == UB_ANY_ID || id->mod_vendor == mod_vendor) &&
		    (module == UB_ANY_ID || id->module == module) &&
		    !((id->class_code ^ class_code) & class_mask)) {
			list_del(&dynid->node);
			kfree(dynid);
			ret = (ssize_t)count;
			break;
		}
	}
	spin_unlock(&udrv->dynids.lock);

	return ret;
}
static DRIVER_ATTR_WO(remove_id);

static struct attribute *ub_drv_attrs[] = {
	&driver_attr_new_id.attr,
	&driver_attr_remove_id.attr,
	NULL
};
ATTRIBUTE_GROUPS(ub_drv);

void ub_bus_type_init(void)
{
	ub_bus_type.match = ub_bus_match;
	ub_bus_type.uevent = ub_uevent;
	ub_bus_type.probe = ub_entity_probe;
	ub_bus_type.remove = ub_entity_remove;
	ub_bus_type.shutdown = ub_entity_shutdown;
	ub_bus_type.dev_groups = ub_entity_groups;
	ub_bus_type.bus_groups = ub_bus_groups;
	ub_bus_type.drv_groups = ub_drv_groups;
}

void ub_bus_type_uninit(void)
{
	ub_bus_type.match = NULL;
	ub_bus_type.uevent = NULL;
	ub_bus_type.probe = NULL;
	ub_bus_type.remove = NULL;
	ub_bus_type.shutdown = NULL;
	ub_bus_type.dev_groups = NULL;
	ub_bus_type.bus_groups = NULL;
	ub_bus_type.drv_groups = NULL;
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

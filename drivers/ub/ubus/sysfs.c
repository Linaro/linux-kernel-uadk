// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) HiSilicon Technologies Co., Ltd. 2025. All rights reserved.
 */

#define pr_fmt(fmt)	"ubus sysfs: " fmt

#include "ubus.h"
#include "sysfs.h"

#define ub_config_attr(field, format_string)	\
static ssize_t field##_show(struct device *dev, struct device_attribute *attr, char *buf)	\
{	\
	struct ub_entity *uent;	\
							\
	uent = to_ub_entity(dev);	\
	return sysfs_emit(buf, format_string, uent->guid.bits.field);	\
}	\
static DEVICE_ATTR_RO(field)

ub_config_attr(device, "%#06x\n");
ub_config_attr(type, "%#x\n");
ub_config_attr(vendor, "%#06x\n");

static ssize_t class_code_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct ub_entity *uent;

	uent = to_ub_entity(dev);
	return sysfs_emit(buf, "%#06x\n", uent->class_code);
}
static DEVICE_ATTR_RO(class_code);

static ssize_t guid_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct ub_entity *uent = to_ub_entity(dev);
	int count;

	count = ub_show_guid(&uent->guid, buf);

	return count + sysfs_emit_at(buf, count, "\n");
}
static DEVICE_ATTR_RO(guid);

static ssize_t kref_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%u\n", kref_read(&dev->kobj.kref));
}
static DEVICE_ATTR_RO(kref);

static ssize_t driver_override_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	struct ub_entity *uent = to_ub_entity(dev);
	int ret;

	ret = driver_set_override(dev, &uent->driver_override, buf, count);
	if (ret)
		return ret;

	return count;
}

static ssize_t driver_override_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct ub_entity *uent = to_ub_entity(dev);
	ssize_t len;

	device_lock(dev);
	len = sysfs_emit(buf, "%s\n", uent->driver_override);
	device_unlock(dev);
	return len;
}
static DEVICE_ATTR_RW(driver_override);

static ssize_t match_driver_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	struct ub_entity *uent = to_ub_entity(dev);
	unsigned long val;
	ssize_t result;

	result = kstrtoul(buf, 0, &val);
	if (result < 0)
		return result;
	if (!(val == 0 || val == 1))
		return -EINVAL;

	device_lock(dev);
	uent->match_driver = !!val;
	device_unlock(dev);

	return count;
}

static ssize_t match_driver_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct ub_entity *uent = to_ub_entity(dev);
	ssize_t len;

	device_lock(dev);
	len = sysfs_emit(buf, "%s\n", uent->match_driver ? "true" : "false");
	device_unlock(dev);

	return len;
}
static DEVICE_ATTR_RW(match_driver);

static struct attribute *ub_entity_attrs[] = {
	&dev_attr_vendor.attr,
	&dev_attr_device.attr,
	&dev_attr_class_code.attr,
	&dev_attr_type.attr,
	&dev_attr_driver_override.attr,
	&dev_attr_match_driver.attr,
	&dev_attr_guid.attr,
	&dev_attr_kref.attr,
	NULL
};

static const struct attribute_group ub_entity_group = {
	.attrs = ub_entity_attrs,
};

const struct attribute_group *ub_entity_groups[] = {
	&ub_entity_group,
	NULL
};

static struct attribute *ub_bus_attrs[] = {
	NULL
};

static const struct attribute_group ub_bus_group = {
	.attrs = ub_bus_attrs,
};

const struct attribute_group *ub_bus_groups[] = {
	&ub_bus_group,
	NULL
};

const struct device_type ub_dev_type = {
	.groups = NULL,
};

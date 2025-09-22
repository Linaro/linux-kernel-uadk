// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) HiSilicon Technologies Co., Ltd. 2025. All rights reserved.
 */

#define pr_fmt(fmt)	"ubus sysfs: " fmt

#include "ubus.h"
#include "sysfs.h"
#include "ubus_entity.h"
#include "instance.h"

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
	&bus_attr_instance.attr,
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

static ssize_t ub_total_entities_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct ub_entity *uent;

	uent = to_ub_entity(dev);
	return sysfs_emit(buf, "%u\n", uent->total_funcs);
}
static DEVICE_ATTR_RO(ub_total_entities);

static ssize_t ub_totalues_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct ub_entity *uent;

	uent = to_ub_entity(dev);
	return sysfs_emit(buf, "%u\n", uent->total_ues);
}
static DEVICE_ATTR_RO(ub_totalues);

static ssize_t ub_numues_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct ub_entity *uent;

	uent = to_ub_entity(dev);
	return sysfs_emit(buf, "%u\n", uent->num_ues);
}

static int ub_numues_store_driver_check(struct ub_entity *uent)
{
	if (!uent->driver) {
		ub_err(uent, "no driver bound to device, enable failed\n");
		return -ENOENT;
	}

	if (!uent->driver->virt_configure) {
		ub_err(uent, "driver does not support virt_configure via sysfs\n");
		return -ENOENT;
	}

	return 0;
}

static void ub_numues_store_ues_process(struct ub_entity *uent, int nums)
{
	int ue_start_idx, ue_end_idx, i, cnt, ret;
	struct ub_entity *ue;

	/*
	 * The software finds "cnt" disabled ues for "ue_start_idx" to
	 * "ue_end_idx" based on the ordered ue_list. "ue_start_idx" is
	 * the entity_idx of first ues and "ue_end_idx" is calculated based
	 * on the total number(that is, "nums") ues to be enabled.
	 */
	ue_start_idx = uent->uem.start_entity_idx;
	ue_end_idx = ue_start_idx + nums - 1;
	i = ue_start_idx;
	cnt = nums - uent->num_ues;
	list_for_each_entry(ue, &uent->ue_list, node) {
		for (; i < ue->entity_idx; i++) {
			/*
			 * The "ue_list" is sorted by entity_idx in ascending order.
			 * Ensure that others ues before this ue are enabled.
			 */
			ret = uent->driver->virt_configure(uent, i, 1);
			if (ret)
				ub_warn(uent, "driver virt_configure, ret=%d\n", ret);
			if (--cnt == 0)
				return;
		}
		/* Skip this enabled ue. */
		i++;
	}

	/* Ensure that the remaining ues enabled. */
	for (; i <= ue_end_idx; i++) {
		ret = uent->driver->virt_configure(uent, i, 1);
		if (ret)
			ub_warn(uent, "driver virt_configure, ret=%d\n", ret);
		if (--cnt == 0)
			return;
	}
}

static ssize_t ub_numues_store(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t count)
{
	struct ub_entity *uent, *ue, *tmp;
	int nums, ret;

	uent = to_ub_entity(dev);

	ret = kstrtoint(buf, 0, &nums);
	if (ret < 0)
		return ret;

	ub_info(uent, "enable %d ue, total_ue=%u\n",
		nums, uent->total_ues);

	if (nums < 0 || nums > uent->total_ues)
		return -EINVAL;

	device_lock(&uent->dev);
	ret = ub_numues_store_driver_check(uent);
	if (ret)
		goto exit;

	if (nums == 0) {
		list_for_each_entry_safe_reverse(ue, tmp, &uent->ue_list, node)
			(void)uent->driver->virt_configure(uent, ue->entity_idx, 0);
		goto exit;
	}

	if (nums <= uent->num_ues || (!entity_flex_en && uent->num_ues)) {
		ub_info(uent, "ue already enabled, num=%u.\n", uent->num_ues);
		goto exit;
	}

	ub_numues_store_ues_process(uent, nums);
exit:
	device_unlock(&uent->dev);
	if (ret < 0)
		return ret;

	return count;
}
static DEVICE_ATTR_RW(ub_numues);

static ssize_t ub_release_ue_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	struct ub_entity *uent, *ue, *tmp;
	unsigned long uent_num;
	int ret;

#define BASE_16 16
	ret = kstrtoul(buf, BASE_16, &uent_num);
	if (ret < 0)
		return ret;

	uent = to_ub_entity(dev);
	device_lock(&uent->dev);
	ret = ub_numues_store_driver_check(uent);
	if (ret)
		goto exit;

	list_for_each_entry_safe_reverse(ue, tmp, &uent->ue_list, node)
		if (ue->uent_num == (u32)uent_num) {
			(void)uent->driver->virt_configure(uent, ue->entity_idx, 0);
			device_unlock(&uent->dev);
			return count;
		}

	ub_err(uent, "uent %#05lx does not belong to this pue\n", uent_num);
exit:
	device_unlock(&uent->dev);
	return -EIO;
}
static DEVICE_ATTR_WO(ub_release_ue);

struct dev_attr_creat_group {
	struct device_attribute *attr;
	bool conditions;
};

static ssize_t ue_list_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	struct ub_entity *uent = to_ub_entity(dev);
	struct ub_entity *ent;
	int cnt = 0;

	list_for_each_entry(ent, &uent->ue_list, node)
		cnt += sysfs_emit_at(buf, cnt, "%s\n", dev_name(&ent->dev));

	return cnt;
}
DEVICE_ATTR_RO(ue_list);

static ssize_t mue_list_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	struct ub_entity *uent = to_ub_entity(dev);
	struct ub_entity *ent;
	int cnt = 0;

	list_for_each_entry(ent, &uent->mue_list, node)
		cnt += sysfs_emit_at(buf, cnt, "%s\n", dev_name(&ent->dev));

	return cnt;
}
DEVICE_ATTR_RO(mue_list);

#define CREATE_CONDITIONS(c) ((c) ? 1 : 0)

static int ub_create_capabilities_sysfs(struct ub_entity *uent)
{
	struct dev_attr_creat_group grp[] = {
		{ &dev_attr_ub_total_entities, CREATE_CONDITIONS(uent->entity_idx == 0) },
		{ &dev_attr_ub_totalues, CREATE_CONDITIONS(uent->is_mue) },
		{ &dev_attr_ub_numues, CREATE_CONDITIONS(uent->is_mue) },
		{ &dev_attr_ub_release_ue, CREATE_CONDITIONS(uent->is_mue && entity_flex_en) },
		{ &dev_attr_mue_list, CREATE_CONDITIONS(uent->entity_idx == 0) },
		{ &dev_attr_ue_list, CREATE_CONDITIONS(uent->is_mue) },
	};
	int retval, i;

	for (i = 0; i < ARRAY_SIZE(grp); i++) {
		if (grp[i].conditions) {
			retval = device_create_file(&uent->dev, grp[i].attr);
			if (retval)
				goto err;
		}
	}

	return 0;
err:
	for (i = i - 1; i >= 0; i--)
		if (grp[i].conditions)
			device_remove_file(&uent->dev, grp[i].attr);
	return retval;
}

static void ub_remove_capabilities_sysfs(struct ub_entity *uent)
{
	struct dev_attr_creat_group grp[] = {
		{ &dev_attr_ub_total_entities, CREATE_CONDITIONS(uent->entity_idx == 0) },
		{ &dev_attr_ub_totalues, CREATE_CONDITIONS(uent->is_mue) },
		{ &dev_attr_ub_numues, CREATE_CONDITIONS(uent->is_mue) },
		{ &dev_attr_ub_release_ue, CREATE_CONDITIONS(uent->is_mue && entity_flex_en) },
		{ &dev_attr_mue_list, CREATE_CONDITIONS(uent->entity_idx == 0) },
		{ &dev_attr_ue_list, CREATE_CONDITIONS(uent->is_mue) },
	};
	int i;

	for (i = 0; i < ARRAY_SIZE(grp); i++)
		if (grp[i].conditions)
			device_remove_file(&uent->dev, grp[i].attr);
}

int ub_create_sysfs_dev_files(struct ub_entity *uent)
{
	int retval;

	retval = ub_create_capabilities_sysfs(uent);
	if (retval)
		return retval;

	return 0;
}

void ub_remove_sysfs_ent_files(struct ub_entity *uent)
{
	ub_remove_capabilities_sysfs(uent);
}

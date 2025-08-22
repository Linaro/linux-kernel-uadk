// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved.
 */
#define pr_fmt(fmt) "UMMU: " fmt

#include <linux/device.h>
#include <linux/errno.h>
#include <linux/iommu.h>
#include <linux/ummu_core.h>

#include "ummu.h"
#include "cfg_table.h"
#include "attribute.h"

#define ATTR_SHOW(dev, capability, buf, ret)                                          \
	do {                                                                          \
		struct ummu_device *ummu =                                            \
			core_to_ummu_device(to_ummu_core(dev_to_iommu_device(dev)));  \
		*(ret) = sysfs_emit(buf, "0x%x\n", ummu->cap.capability);             \
	} while (0)

#define ATTR_SHOW_64(dev, capability, buf, ret)                                       \
	do {                                                                          \
		struct ummu_device *ummu =                                            \
			core_to_ummu_device(to_ummu_core(dev_to_iommu_device(dev)));  \
		*(ret) = sysfs_emit(buf, "0x%llx\n", ummu->cap.capability);           \
	} while (0)

static ssize_t features_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	int ret;

	ATTR_SHOW(dev, features, buf, &ret);
	return ret;
}
static DEVICE_ATTR_RO(features);

static ssize_t tid_bits_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	int ret;

	ATTR_SHOW(dev, tid_bits, buf, &ret);
	return ret;
}
static DEVICE_ATTR_RO(tid_bits);

static ssize_t pgsize_bitmap_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	int ret;

	ATTR_SHOW_64(dev, pgsize_bitmap, buf, &ret);
	return ret;
}
static DEVICE_ATTR_RO(pgsize_bitmap);

static ssize_t ias_show(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	int ret;

	ATTR_SHOW(dev, ias, buf, &ret);
	return ret;
}
static DEVICE_ATTR_RO(ias);

static ssize_t oas_show(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	int ret;

	ATTR_SHOW(dev, oas, buf, &ret);
	return ret;
}
static DEVICE_ATTR_RO(oas);

static ssize_t ptsize_bitmap_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	int ret;

	ATTR_SHOW_64(dev, ptsize_bitmap, buf, &ret);
	return ret;
}
static DEVICE_ATTR_RO(ptsize_bitmap);

static ssize_t options_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	int ret;

	ATTR_SHOW(dev, options, buf, &ret);
	return ret;
}
static DEVICE_ATTR_RO(options);

static ssize_t mcmdq_log2num_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	int ret;

	ATTR_SHOW(dev, mcmdq_log2num, buf, &ret);
	return ret;
}
static DEVICE_ATTR_RO(mcmdq_log2num);

static ssize_t mcmdq_log2size_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	int ret;

	ATTR_SHOW(dev, mcmdq_log2size, buf, &ret);
	return ret;
}
static DEVICE_ATTR_RO(mcmdq_log2size);

static ssize_t evtq_log2num_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	int ret;

	ATTR_SHOW(dev, evtq_log2num, buf, &ret);
	return ret;
}
static DEVICE_ATTR_RO(evtq_log2num);

static ssize_t evtq_log2size_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	int ret;

	ATTR_SHOW(dev, evtq_log2size, buf, &ret);
	return ret;
}
static DEVICE_ATTR_RO(evtq_log2size);

static ssize_t permq_num_show(struct device *dev, struct device_attribute *attr,
			      char *buf)
{
	int ret;

	ATTR_SHOW(dev, permq_num, buf, &ret);
	return ret;
}
static DEVICE_ATTR_RO(permq_num);

static ssize_t permq_ent_num_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct ummu_device *ummu =
		core_to_ummu_device(to_ummu_core(dev_to_iommu_device(dev)));
	u32 pcplq_ent_num, pcmdq_ent_num;

	pcplq_ent_num = ummu->cap.permq_ent_num.cplq_num;
	pcmdq_ent_num = ummu->cap.permq_ent_num.cmdq_num;
	return sysfs_emit(buf, "pcmdq 0x%x pcplq 0x%x\n", pcmdq_ent_num,
		       pcplq_ent_num);
}
static DEVICE_ATTR_RO(permq_ent_num);

static ssize_t eid_list_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	char *str = ummu_get_eid_list();
	size_t len;

	if (!str)
		return -EINVAL;

	len = strlen(str);
	if (len >= PAGE_SIZE)
		len = PAGE_SIZE - 1;

	memcpy(buf, str, len);
	buf[len] = '\0';
	kfree(str);
	return len;
}
static DEVICE_ATTR_RO(eid_list);

static const char *get_domain_type_str(u32 domain_type)
{
	switch (domain_type) {
	case IOMMU_DOMAIN_DMA:
		return "IOMMU_DOMAIN_DMA";
	case IOMMU_DOMAIN_SVA:
		return "IOMMU_DOMAIN_SVA";
	default:
		return "UNKNOWN DOMAIN TYPE";
	}
}

static ssize_t tid_type_store(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf, size_t count)
{
	struct ummu_core_device *ummu_core;
	u32 tid = 0, tid_type;
	int ret;

	ret = kstrtouint(buf, 0, &tid);
	if (ret < 0 || tid >= UMMU_INVALID_TID)
		return -EINVAL;

	ummu_core = to_ummu_core(dev_to_iommu_device(dev));
	ret = ummu_core_get_tid_type(ummu_core, tid, &tid_type);
	if (ret) {
		pr_err("Invalid tid = 0x%x, ret = %d.\n", tid, ret);
		return ret;
	}

	pr_info("tid = 0x%x, domain_type = %s.\n", tid,
		get_domain_type_str(tid_type));

	return (ssize_t)count;
}
static DEVICE_ATTR_WO(tid_type);

static struct attribute *ummu_iommu_attrs[] = {
	&dev_attr_features.attr,
	&dev_attr_tid_bits.attr,
	&dev_attr_pgsize_bitmap.attr,
	&dev_attr_ias.attr,
	&dev_attr_oas.attr,
	&dev_attr_ptsize_bitmap.attr,
	&dev_attr_options.attr,
	&dev_attr_mcmdq_log2num.attr,
	&dev_attr_mcmdq_log2size.attr,
	&dev_attr_evtq_log2num.attr,
	&dev_attr_evtq_log2size.attr,
	&dev_attr_permq_num.attr,
	&dev_attr_permq_ent_num.attr,
	&dev_attr_eid_list.attr,
	&dev_attr_tid_type.attr,
	NULL,
};

static struct attribute_group ummu_iommu_group = {
	.name = "ummu-iommu",
	.attrs = ummu_iommu_attrs,
};

static const struct attribute_group *ummu_iommu_groups[] = {
	&ummu_iommu_group,
	NULL,
};

const struct attribute_group **get_attribute_group(void)
{
	return ummu_iommu_groups;
};

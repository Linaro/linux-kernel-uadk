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
#include "qos.h"
#include "attribute.h"

#define NUMBER_BASE_DECIMAL 10

static int g_partid[UMMU_MPAM_TYPE_NUM] = { 0 };
static int g_pmg[UMMU_MPAM_TYPE_NUM] = { 0 };

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

static int check_input_buf(const char *buf)
{
	int var, ret;

	ret = kstrtoint(buf, NUMBER_BASE_DECIMAL, &var);
	if (ret)
		return ret;

	if (var != 1)
		return -EPERM;
	return 0;
}

static struct ummu_device *attr_get_ummu_device(struct device *dev)
{
	struct iommu_device *iommu;

	if (!dev)
		return NULL;

	iommu = (struct iommu_device *)dev_get_drvdata(dev);
	if (!iommu)
		return NULL;

	return core_to_ummu_device(to_ummu_core(iommu));
}

static ssize_t bp_partid_store(struct device *kobj, struct device_attribute *attr,
			       const char *buf, size_t count)
{
	int var, ret;

	ret = kstrtoint(buf, NUMBER_BASE_DECIMAL, &var);
	if (ret < 0)
		return ret;

	g_partid[UMMU_BYPASS_MPAM] = var;
	dev_info(kobj, "ummu bypass_mpam input partid = %d.\n", var);
	return count;
}

static ssize_t bp_pmg_store(struct device *kobj, struct device_attribute *attr,
			    const char *buf, size_t count)
{
	int var, ret;

	ret = kstrtoint(buf, NUMBER_BASE_DECIMAL, &var);
	if (ret < 0)
		return ret;

	g_pmg[UMMU_BYPASS_MPAM] = var;
	dev_info(kobj, "ummu bypass_mpam input pmg = %d.\n", var);
	return count;
}

static ssize_t bp_run_store(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t count)
{
	struct ummu_device *ummu;
	int ret;

	ret = check_input_buf(buf);
	if (ret)
		return ret;

	ummu = attr_get_ummu_device(dev);
	if (!ummu)
		return -ENODEV;

	dev_info(dev, "ummu set bypass_mpam(partid = %d, pmg = %d)\n",
		g_partid[UMMU_BYPASS_MPAM], g_pmg[UMMU_BYPASS_MPAM]);

	ret = ummu_set_bypass_mpam(ummu,
				   g_partid[UMMU_BYPASS_MPAM],
				   g_pmg[UMMU_BYPASS_MPAM]);
	if (ret) {
		dev_err(dev, "ummu set bypass_mpam failed, ret = %d.\n", ret);
		return ret;
	}
	return count;
}

static ssize_t bp_mpam_info_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct ummu_device *ummu;
	int partid = 0, pmg = 0, ret;

	ummu = attr_get_ummu_device(dev);
	if (!ummu)
		return -ENODEV;

	ret = ummu_get_bypass_mpam(ummu, &partid, &pmg);
	if (ret)
		return ret;
	return sysfs_emit(buf, "partid:%d\npmg:%d\n", partid, pmg);
}

static DEVICE_ATTR_WO(bp_partid);
static DEVICE_ATTR_WO(bp_pmg);
static DEVICE_ATTR_WO(bp_run);
static DEVICE_ATTR_RO(bp_mpam_info);

static struct attribute *ummu_bypass_mpam_attrs[] = {
	&dev_attr_bp_partid.attr,
	&dev_attr_bp_pmg.attr,
	&dev_attr_bp_run.attr,
	&dev_attr_bp_mpam_info.attr,
	NULL,
};

static struct attribute_group ummu_bypass_mpam_group = {
	.name = "ummu_bypass_mpam",
	.attrs = ummu_bypass_mpam_attrs,
};

static ssize_t uotr_partid_store(struct device *kobj, struct device_attribute *attr,
				 const char *buf, size_t count)
{
	int var, ret;

	ret = kstrtoint(buf, NUMBER_BASE_DECIMAL, &var);
	if (ret < 0)
		return ret;

	g_partid[UMMU_UOTR_MPAM] = var;
	dev_info(kobj, "ummu uotr_mpam input partid = %d.\n", var);
	return count;
}

static ssize_t uotr_pmg_store(struct device *kobj, struct device_attribute *attr,
			      const char *buf, size_t count)
{
	int var, ret;

	ret = kstrtoint(buf, NUMBER_BASE_DECIMAL, &var);
	if (ret < 0)
		return ret;

	g_pmg[UMMU_UOTR_MPAM] = var;
	dev_info(kobj, "ummu uotr_mpam input pmg = %d.\n", var);
	return count;
}

static ssize_t uotr_run_store(struct device *dev, struct device_attribute *attr,
			      const char *buf, size_t count)
{
	struct ummu_device *ummu;
	int ret;

	ret = check_input_buf(buf);
	if (ret)
		return ret;

	ummu = attr_get_ummu_device(dev);
	if (!ummu)
		return -ENODEV;

	dev_info(dev, "ummu set uotr_mpam(partid = %d, pmg = %d)\n",
		g_partid[UMMU_UOTR_MPAM], g_pmg[UMMU_UOTR_MPAM]);

	ret = ummu_set_uotr_mpam(ummu,
				 g_partid[UMMU_UOTR_MPAM],
				 g_pmg[UMMU_UOTR_MPAM]);
	if (ret) {
		dev_err(dev, "ummu set uotr_mpam failed, ret = %d.\n", ret);
		return ret;
	}
	return count;
}

static ssize_t uotr_mpam_info_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct ummu_device *ummu;
	int partid = 0, pmg = 0, ret;

	ummu = attr_get_ummu_device(dev);
	if (!ummu)
		return -ENODEV;

	ret = ummu_get_uotr_mpam(ummu, &partid, &pmg);
	if (ret)
		return ret;
	return sysfs_emit(buf, "partid:%d\npmg:%d\n", partid, pmg);
}

static DEVICE_ATTR_WO(uotr_partid);
static DEVICE_ATTR_WO(uotr_pmg);
static DEVICE_ATTR_WO(uotr_run);
static DEVICE_ATTR_RO(uotr_mpam_info);

static struct attribute *ummu_uotr_mpam_attrs[] = {
	&dev_attr_uotr_partid.attr,
	&dev_attr_uotr_pmg.attr,
	&dev_attr_uotr_run.attr,
	&dev_attr_uotr_mpam_info.attr,
	NULL,
};

static struct attribute_group ummu_uotr_mpam_group = {
	.name = "ummu_uotr_mpam",
	.attrs = ummu_uotr_mpam_attrs,
};

static const struct attribute_group *ummu_iommu_groups[] = {
	&ummu_iommu_group,
	/*
	 * bypass_mpam: Memory traffic monitoring
	 * of the UB device when ummu is bypassed.
	 */
	&ummu_bypass_mpam_group,
	/*
	 * uotr_mpam: Memory traffic monitoring
	 * of ummu-originated transactions
	 * relating to the Non-secure programming interface.
	 */
	&ummu_uotr_mpam_group,
	NULL,
};

const struct attribute_group **get_attribute_group(void)
{
	return ummu_iommu_groups;
};

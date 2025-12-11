// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) HiSilicon Technologies Co., Ltd. 2025. All rights reserved.
 * Descriptor:
 * Parse and create ummu from UBRT(reported by ACPI) or UBIOS Info table (reported by DTS)
 */

#define pr_fmt(fmt)	"ubfi ummu: " fmt

#include <linux/platform_device.h>
#include <ub/ubfi/ubfi.h>
#include <linux/of_platform.h>

#include "ubrt.h"
#include "ub_fi.h"
#include "ubc.h"
#include "ummu.h"

struct ummu_sub_table {
	struct ub_table_header header;
	u16 count;
	u8 reserved[6];
	u8 node_data[]; __counted_by(count)
};

#define UBRT_UMMU_PXM_VALID 0xFFFF
#define ACPI_UMMU_DEVICE_HID "HISI0551"
#define ACPI_UMMU_PMU_DEVICE_HID "HISI0571"
#define UMMU_INDEX_MASK GENMASK(31, 0)
#define UMMU_TYPE_MASK GENMASK_ULL(63, 32)

static int __init ummu_set_proximity(struct device *dev, struct ummu_node *node)
{
	int dev_node;

	if (node->pxm == UBRT_UMMU_PXM_VALID)
		return 0;

	if (acpi_disabled) {
		dev_node = node->pxm;
		if (dev_node >= MAX_NUMNODES || !node_possible(dev_node))
			return -EINVAL;
	} else {
		dev_node = pxm_to_node(node->pxm);
		if (dev_node != NUMA_NO_NODE && !node_online(dev_node))
			return -EINVAL;
	}

	set_dev_node(dev, dev_node);

	dev_info(dev, "UMMU mapped to Proximity domain : %u dev_node : %d\n",
		 node->pxm, dev_node);

	return 0;
}

static int __init ummu_count_resources(struct ummu_node *node)
{
	/* present mem resource only */
	return 1;
}

static void __init ummu_pmu_dev_init_resources(struct resource *res, int cnt,
					       struct ummu_node *node)
{
	int num = 0;

	res[num].start = node->pmu_addr;
	res[num].end = node->pmu_addr + node->pmu_size - 1;
	res[num].flags = IORESOURCE_MEM;
	num++;

	if (num != cnt)
		pr_err("ummu pmu res num is not match!\n");
}

static void __init ummu_device_init_resources(struct resource *res, int cnt,
				       struct ummu_node *node)
{
	int num = 0;

	res[num].start = node->base_addr;
	res[num].end = node->base_addr + node->addr_size - 1;
	res[num].flags = IORESOURCE_MEM;
	num++;
	if (num != cnt)
		pr_err("ummu res num is not match!\n");
}

static int __init ummu_add_resources(struct platform_device *pdev,
				     struct ummu_node *node,
				     enum ubrt_node_type type)
{
	struct resource *res __free(kfree);
	int num_res;

	num_res = ummu_count_resources(node);
	res = kcalloc(num_res, sizeof(*res), GFP_KERNEL);
	if (!res)
		return -ENOMEM;

	if (type == UBRT_UMMU)
		ummu_device_init_resources(res, num_res, node);
	else
		ummu_pmu_dev_init_resources(res, num_res, node);

	return platform_device_add_resources(pdev, res, num_res);
}

static int ummu_rename_device(struct platform_device *pdev, enum ubrt_node_type type)
{
	static int device_count;
	char new_name[32];
	int ret;

	if (type == UBRT_UMMU)
		ret = snprintf(new_name, sizeof(new_name), "ummu.%d", device_count);
	else
		ret = snprintf(new_name, sizeof(new_name), "ummu_pmu.%d", device_count);

	if (ret < 0 || ret >= sizeof(new_name)) {
		dev_err(&pdev->dev, "failed to generate new device name\n");
		return -ENOENT;
	}

	ret = device_rename(&pdev->dev, new_name);
	if (ret) {
		dev_err(&pdev->dev, "failed to rename device to %s: %d\n", new_name, ret);
		return ret;
	}
	pdev->name = pdev->dev.kobj.name;

	device_count++;

	return 0;
}

static int ummu_config_update(struct platform_device *pdev,
			      struct ummu_node *ummu_node,
			      enum ubrt_node_type type)
{
	int ret;

	if (!pdev->dev.msi.domain)
		dev_warn(&pdev->dev, "can't find device msi domain.\n");

	ret = ummu_rename_device(pdev, type);
	if (ret)
		return ret;

	ret = ummu_set_proximity(&pdev->dev, ummu_node);
	if (ret) {
		dev_err(&pdev->dev, "ummu set proximity failed ret[%d]\n", ret);
		return ret;
	}

	ret = ummu_add_resources(pdev, ummu_node, type);
	if (ret) {
		dev_err(&pdev->dev, "ummu add resources failed ret[%d]\n", ret);
		return ret;
	}

	if (type == UBRT_UMMU) {
		ret = platform_device_add_data(pdev, ummu_node->vendor_info,
						sizeof(ummu_node->vendor_info));
		if (ret)
			return ret;
	}

	dev_info(&pdev->dev, "interrupt_id[0x%x], pxm[%u], its_index[%u], vendor_id[0x%x]\n",
		 ((type == UBRT_UMMU) ? ummu_node->intr_id : ummu_node->pmu_intr_id),
		 ummu_node->pxm, ummu_node->its_index, ummu_node->vendor_id);
	return 0;
}

static acpi_status acpi_processor_ummu(acpi_handle handle, u32 lvl,
				      void *context, void **rv)
{
	struct platform_device *pdev;
	struct acpi_device *adev;
	enum ubrt_node_type type;
	struct ummu_node *node;
	struct ubrt_fwnode *fw;
	unsigned long long uid;
	acpi_status status;
	struct device *dev;
	u64 *node_flag;
	u32 index;
	int ret;

	node_flag = context;
	index = FIELD_GET(UMMU_INDEX_MASK, *node_flag);
	type = FIELD_GET(UMMU_TYPE_MASK, *node_flag);
	fw = ubrt_fwnode_get_by_idx(index, type);
	if (!fw) {
		pr_err("can not get ubrt fwnode!\n");
		return AE_CTRL_FALSE;
	}

	status = acpi_evaluate_integer(handle, "_UID", NULL, &uid);
	if (ACPI_FAILURE(status)) {
		pr_err("can not get dsdt uid. status[%u]\n", status);
		return AE_CTRL_TERMINATE;
	}

	if (index != (u32)uid)
		return AE_OK;

	adev = acpi_get_acpi_dev(handle);
	if (!adev) {
		pr_err("acpi get device failed\n");
		return AE_CTRL_TERMINATE;
	}

	dev = bus_find_device_by_acpi_dev(&platform_bus_type, adev);
	if (!dev) {
		pr_err("platform device not found\n");
		status = AE_CTRL_TERMINATE;
		goto out;
	}
	pdev = to_platform_device(dev);
	node = (struct ummu_node *)fw->ubrt_node;

	ret = ummu_config_update(pdev, node, type);
	if (ret) {
		dev_err(dev, "update config failed, ret[%d]\n", ret);
		status = AE_CTRL_FALSE;
		goto out;
	}

	ret = ubrt_fwnode_set(index, type, dev->fwnode);
	if (ret) {
		dev_err(dev, "update fwnode failed, ret[%d]\n", ret);
		status = AE_CTRL_FALSE;
	}

out:
	acpi_put_acpi_dev(adev);
	return status;
}

#ifdef CONFIG_ACPI
static int acpi_update_ummu_config(struct ummu_node *ummu_node, u32 index)
{
	acpi_status status;
	u64 node_flag;
	int ret;

	ret = ubrt_fwnode_add(ummu_node, index, sizeof(*ummu_node), UBRT_UMMU);
	if (ret) {
		pr_err("failed to add ummu fwnode! ret[%d]\n", ret);
		return ret;
	}

	node_flag = index | (((u64)UBRT_UMMU) << 32);

	status = acpi_get_devices(ACPI_UMMU_DEVICE_HID,
				  acpi_processor_ummu,
				  &node_flag, NULL);
	if (ACPI_FAILURE(status)) {
		pr_err("acpi get devices err, status[%u]\n", status);
		goto ummu_err;
	}

	ret = ubrt_fwnode_add(ummu_node, index, sizeof(struct ummu_node), UBRT_UMMU_PMU);
	if (ret) {
		pr_err("failed to add pmu fwnode! ret[%d]\n", ret);
		goto ummu_err;
	}

	node_flag = index | (((u64)UBRT_UMMU_PMU) << 32);

	/* Get UB PMU from DSDT */
	status = acpi_get_devices(ACPI_UMMU_PMU_DEVICE_HID,
				  acpi_processor_ummu,
				  &node_flag, NULL);
	if (ACPI_FAILURE(status)) {
		pr_err("acpi get devices err, status[%u]\n", status);
		goto pmu_err;
	}

	return 0;

pmu_err:
	ubrt_fwnode_del(index, UBRT_UMMU_PMU);
ummu_err:
	ubrt_fwnode_del(index, UBRT_UMMU);
	return -ENODEV;
}
#else
static inline int acpi_update_ummu_config(struct ummu_node *ummu_node, u32 index)
{
	return -ENODEV;
}
#endif /* CONFIG_ACPI */

#ifdef CONFIG_OF
static struct platform_device *ummu_of_find_plat_dev(struct device_node *node, u32 index)
{
	struct platform_device *pdev;
	const char *node_name;
	u32 dts_index;
	int ret;

	node_name = of_node_full_name(node);

	ret = of_property_read_u32(node, "index", &dts_index);
	if (ret) {
		pr_err("dts can't find ummu ctl-no\n");
		return NULL;
	}

	if (dts_index != index) {
		pr_debug("ummu dts_index %u != index %u\n", dts_index, index);
		return NULL;
	}

	pdev = of_find_device_by_node(node);
	if (!pdev) {
		pr_err("failed to find platform device for node: %s\n", node_name);
		return NULL;
	}

	return pdev;
}

static int ummu_of_update_config(struct platform_device *pdev,
				 struct ummu_node *ummu_node,
				 u32 index,
				 enum ubrt_node_type type)
{
	int ret;

	ret = ubrt_fwnode_add(ummu_node, index, sizeof(struct ummu_node), type);
	if (ret) {
		dev_err(&pdev->dev, "failed to add ummu fwnode! ret[%d]\n", ret);
		return ret;
	}

	ret = ubrt_fwnode_set(index, type, pdev->dev.fwnode);
	if (ret) {
		dev_err(&pdev->dev, "update fwnode failed, ret[%d]\n", ret);
		goto err;
	}

	ret = ummu_config_update(pdev, ummu_node, type);
	if (ret) {
		dev_err(&pdev->dev, "update config failed, ret[%d]\n", ret);
		goto err;
	}

	return 0;
err:
	ubrt_fwnode_del(index, type);
	return ret;
}

static int dts_update_ummu_config(struct ummu_node *ummu_node, u32 index)
{
	struct platform_device *pdev;
	struct device_node *node;
	int ret;

	for_each_compatible_node(node, NULL, "ub,ummu") {
		pdev = ummu_of_find_plat_dev(node, index);
		if (!pdev)
			continue;

		ret = ummu_of_update_config(pdev, ummu_node, index, UBRT_UMMU);
		if (ret)
			return ret;
	}

	for_each_compatible_node(node, NULL, "ub,ummu_pmu") {
		pdev = ummu_of_find_plat_dev(node, index);
		if (!pdev)
			continue;

		ret = ummu_of_update_config(pdev, ummu_node, index, UBRT_UMMU_PMU);
		if (ret)
			return ret;
	}

	return 0;
}
#else
static inline int dts_update_ummu_config(struct ummu_node *ummu_node, u32 index)
{
	return -ENODEV;
}
#endif  /* CONFIG_OF */

static int parse_ummu(void *info_node)
{
	struct ummu_sub_table *sub_table;
	struct ummu_node *ummu_node;
	int ret;
	u32 index;

	sub_table = (struct ummu_sub_table *)info_node;
	if (!sub_table->count) {
		pr_warn("info table has no ummu.\n");
		return 0;
	}

	pr_info("ummu node num: %u\n", sub_table->count);

	ummu_node = (struct ummu_node *)sub_table->node_data;
	for (index = 0; index < sub_table->count; index++, ummu_node++) {
		if (acpi_disabled)
			ret = dts_update_ummu_config(ummu_node, index);
		else
			ret = acpi_update_ummu_config(ummu_node, index);

		if (ret) {
			pr_err("Create No.%u ummu failed, ret=%d\n", index, ret);
			return ret;
		}
	}

	return 0;
}

int handle_ummu_table(u64 pointer)
{
	void *info_node = ub_table_get(pointer);
	int ret;

	if (!info_node)
		return -EINVAL;

	ret = parse_ummu(info_node);
	ub_table_put(info_node);
	return ret;
}

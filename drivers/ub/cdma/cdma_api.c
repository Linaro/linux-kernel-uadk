// SPDX-License-Identifier: GPL-2.0+
/* Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved. */

#define pr_fmt(fmt) "CDMA: " fmt
#define dev_fmt pr_fmt

#include "cdma_dev.h"
#include "cdma_cmd.h"
#include "cdma_context.h"
#include "cdma.h"
#include <ub/cdma/cdma_api.h>

struct dma_device *dma_get_device_list(u32 *num_devices)
{
	struct cdma_device_attr *attr;
	struct xarray *cdma_devs_tbl;
	struct cdma_dev *cdev = NULL;
	struct dma_device *ret_list;
	unsigned long index;
	u32 count = 0;
	u32 devs_num;

	if (!num_devices)
		return NULL;

	cdma_devs_tbl = get_cdma_dev_tbl(&devs_num);
	if (!devs_num) {
		pr_err("cdma device table is empty.\n");
		return NULL;
	}

	ret_list = kcalloc(devs_num, sizeof(struct dma_device), GFP_KERNEL);
	if (!ret_list) {
		*num_devices = 0;
		return NULL;
	}

	xa_for_each(cdma_devs_tbl, index, cdev) {
		attr = &cdev->base.attr;
		if (cdev->status == CDMA_SUSPEND) {
			pr_warn("cdma device is not prepared, eid = 0x%x.\n",
				attr->eid.dw0);
			continue;
		}

		if (!attr->eu_num) {
			pr_warn("no eu in cdma dev eid = 0x%x.\n", cdev->eid);
			continue;
		}

		memcpy(&attr->eu, &attr->eus[0], sizeof(attr->eu));
		attr->eid.dw0 = cdev->eid;
		memcpy(&ret_list[count], &cdev->base, sizeof(*ret_list));
		ret_list[count].private_data = kzalloc(
			sizeof(struct cdma_ctx_res), GFP_KERNEL);
		if (!ret_list[count].private_data)
			break;
		count++;
	}
	*num_devices = count;

	return ret_list;
}
EXPORT_SYMBOL_GPL(dma_get_device_list);

void dma_free_device_list(struct dma_device *dev_list, u32 num_devices)
{
	int ref_cnt;
	u32 i;

	if (!dev_list)
		return;

	for (i = 0; i < num_devices; i++) {
		ref_cnt = atomic_read(&dev_list[i].ref_cnt);
		if (ref_cnt > 0) {
			pr_warn("the device resourse is still in use, eid = 0x%x, cnt = %d.\n",
				dev_list[i].attr.eid.dw0, ref_cnt);
			return;
		}
	}

	for (i = 0; i < num_devices; i++)
		kfree(dev_list[i].private_data);

	kfree(dev_list);
}
EXPORT_SYMBOL_GPL(dma_free_device_list);

struct dma_device *dma_get_device_by_eid(struct dev_eid *eid)
{
	struct cdma_device_attr *attr;
	struct xarray *cdma_devs_tbl;
	struct cdma_dev *cdev = NULL;
	struct dma_device *ret_dev;
	unsigned long index;
	u32 devs_num;

	if (!eid)
		return NULL;

	cdma_devs_tbl = get_cdma_dev_tbl(&devs_num);
	if (!devs_num) {
		pr_err("cdma device table is empty.\n");
		return NULL;
	}

	ret_dev = kzalloc(sizeof(struct dma_device), GFP_KERNEL);
	if (!ret_dev)
		return NULL;

	xa_for_each(cdma_devs_tbl, index, cdev) {
		attr = &cdev->base.attr;
		if (cdev->status == CDMA_SUSPEND) {
			pr_warn("cdma device is not prepared, eid = 0x%x.\n",
				attr->eid.dw0);
			continue;
		}

		if (!cdma_find_seid_in_eus(attr->eus, attr->eu_num, eid,
					   &attr->eu))
			continue;

		memcpy(ret_dev, &cdev->base, sizeof(*ret_dev));
		ret_dev->private_data = kzalloc(
			sizeof(struct cdma_ctx_res), GFP_KERNEL);
		if (!ret_dev->private_data) {
			kfree(ret_dev);
			return NULL;
		}
		return ret_dev;
	}
	kfree(ret_dev);

	return NULL;
}
EXPORT_SYMBOL_GPL(dma_get_device_by_eid);

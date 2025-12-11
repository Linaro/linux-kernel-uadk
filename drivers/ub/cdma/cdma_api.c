// SPDX-License-Identifier: GPL-2.0+
/* Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved. */

#define pr_fmt(fmt) "CDMA: " fmt
#define dev_fmt pr_fmt

#include "cdma_segment.h"
#include "cdma_dev.h"
#include "cdma_cmd.h"
#include "cdma_context.h"
#include "cdma_queue.h"
#include "cdma_jfc.h"
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

int dma_create_context(struct dma_device *dma_dev)
{
	struct cdma_ctx_res *ctx_res;
	struct cdma_context *ctx;
	struct cdma_dev *cdev;

	if (!dma_dev || !dma_dev->private_data) {
		pr_err("the dma_dev does not exist.\n");
		return -EINVAL;
	}

	cdev = get_cdma_dev_by_eid(dma_dev->attr.eid.dw0);
	if (!cdev) {
		pr_err("can't find cdev by eid, eid = 0x%x\n",
		       dma_dev->attr.eid.dw0);
		return -EINVAL;
	}

	if (cdev->status == CDMA_SUSPEND) {
		pr_warn("cdma device is not prepared, eid = 0x%x.\n",
			dma_dev->attr.eid.dw0);
		return -EINVAL;
	}

	ctx_res = (struct cdma_ctx_res *)dma_dev->private_data;
	if (ctx_res->ctx) {
		pr_err("ctx has been created.\n");
		return -EEXIST;
	}

	atomic_inc(&dma_dev->ref_cnt);
	ctx = cdma_alloc_context(cdev, true);
	if (IS_ERR(ctx)) {
		pr_err("alloc context failed, ret = %ld\n", PTR_ERR(ctx));
		atomic_dec(&dma_dev->ref_cnt);
		return PTR_ERR(ctx);
	}
	ctx_res->ctx = ctx;

	return ctx->handle;
}
EXPORT_SYMBOL_GPL(dma_create_context);

void dma_delete_context(struct dma_device *dma_dev, int handle)
{
	struct cdma_ctx_res *ctx_res;
	struct cdma_context *ctx;
	struct cdma_dev *cdev;
	int ref_cnt;

	if (!dma_dev || !dma_dev->private_data)
		return;

	cdev = get_cdma_dev_by_eid(dma_dev->attr.eid.dw0);
	if (!cdev) {
		pr_err("can't find cdev by eid, eid = 0x%x\n",
		       dma_dev->attr.eid.dw0);
		return;
	}

	ctx_res = (struct cdma_ctx_res *)dma_dev->private_data;
	ctx = ctx_res->ctx;
	if (!ctx) {
		dev_err(cdev->dev, "no context needed to be free\n");
		return;
	}

	ref_cnt = atomic_read(&ctx->ref_cnt);
	if (ref_cnt > 0) {
		dev_warn(cdev->dev,
			 "context resourse is still in use, cnt = %d.\n",
			 ref_cnt);
		return;
	}

	cdma_free_context(cdev, ctx);
	ctx_res->ctx = NULL;
	atomic_dec(&dma_dev->ref_cnt);
}
EXPORT_SYMBOL_GPL(dma_delete_context);

int dma_alloc_queue(struct dma_device *dma_dev, int ctx_id, struct queue_cfg *cfg)
{
	struct cdma_ctx_res *ctx_res;
	struct cdma_queue *queue;
	struct cdma_context *ctx;
	struct cdma_dev *cdev;
	int ret;

	if (!cfg || !dma_dev || !dma_dev->private_data)
		return -EINVAL;

	cdev = get_cdma_dev_by_eid(dma_dev->attr.eid.dw0);
	if (!cdev) {
		pr_err("can't find cdev by eid, eid = 0x%x.\n",
		       dma_dev->attr.eid.dw0);
		return -EINVAL;
	}

	if (cdev->status == CDMA_SUSPEND) {
		pr_warn("cdma device is not prepared, eid = 0x%x.\n",
			dma_dev->attr.eid.dw0);
		return -EINVAL;
	}

	ctx = cdma_find_ctx_by_handle(cdev, ctx_id);
	if (!ctx) {
		dev_err(cdev->dev, "invalid ctx_id = %d.\n", ctx_id);
		return -EINVAL;
	}
	atomic_inc(&ctx->ref_cnt);

	queue = cdma_create_queue(cdev, ctx, cfg, dma_dev->attr.eu.eid_idx,
				  true);
	if (!queue) {
		dev_err(cdev->dev, "create queue failed.\n");
		ret = -EINVAL;
		goto decrease_cnt;
	}

	ctx_res = (struct cdma_ctx_res *)dma_dev->private_data;
	ret = xa_err(
		xa_store(&ctx_res->queue_xa, queue->id, queue, GFP_KERNEL));
	if (ret) {
		dev_err(cdev->dev, "store queue to ctx_res failed, ret = %d\n",
			ret);
		goto free_queue;
	}

	return queue->id;

free_queue:
	cdma_delete_queue(cdev, queue->id);
decrease_cnt:
	atomic_dec(&ctx->ref_cnt);
	return ret;
}
EXPORT_SYMBOL_GPL(dma_alloc_queue);

void dma_free_queue(struct dma_device *dma_dev, int queue_id)
{
	struct cdma_ctx_res *ctx_res;
	struct cdma_context *ctx;
	struct cdma_queue *queue;
	struct cdma_dev *cdev;

	if (!dma_dev || !dma_dev->private_data)
		return;

	cdev = get_cdma_dev_by_eid(dma_dev->attr.eid.dw0);
	if (!cdev) {
		pr_err("can't find cdev by eid, eid = 0x%x\n",
		       dma_dev->attr.eid.dw0);
		return;
	}

	ctx_res = (struct cdma_ctx_res *)dma_dev->private_data;
	queue = (struct cdma_queue *)xa_load(&ctx_res->queue_xa, queue_id);
	if (!queue) {
		dev_err(cdev->dev, "no queue found in this device, id = %d\n",
			queue_id);
		return;
	}
	xa_erase(&ctx_res->queue_xa, queue_id);
	ctx = queue->ctx;

	cdma_delete_queue(cdev, queue_id);

	atomic_dec(&ctx->ref_cnt);
}
EXPORT_SYMBOL_GPL(dma_free_queue);

struct dma_seg *dma_register_seg(struct dma_device *dma_dev, int ctx_id,
				 struct dma_seg_cfg *cfg)
{
	struct cdma_ctx_res *ctx_res;
	struct cdma_segment *seg;
	struct cdma_context *ctx;
	struct dma_seg *ret_seg;
	struct cdma_dev *cdev;
	int ret;

	if (!dma_dev || !dma_dev->private_data || !cfg || !cfg->sva || !cfg->len)
		return NULL;

	cdev = get_cdma_dev_by_eid(dma_dev->attr.eid.dw0);
	if (!cdev) {
		pr_err("can not find normal cdev by eid, eid = 0x%x\n",
		       dma_dev->attr.eid.dw0);
		return NULL;
	}

	if (cdev->status == CDMA_SUSPEND) {
		pr_warn("cdma device is not prepared, eid = 0x%x.\n",
			dma_dev->attr.eid.dw0);
		return NULL;
	}

	ctx = cdma_find_ctx_by_handle(cdev, ctx_id);
	if (!ctx) {
		dev_err(cdev->dev, "find ctx by handle failed, handle = %d.\n",
			ctx_id);
		return NULL;
	}
	atomic_inc(&ctx->ref_cnt);

	seg = cdma_register_seg(cdev, cfg, true);
	if (!seg)
		goto decrease_cnt;

	seg->ctx = ctx;
	ret = cdma_seg_grant(cdev, seg, cfg);
	if (ret)
		goto unregister_seg;

	ret_seg = kzalloc(sizeof(struct dma_seg), GFP_KERNEL);
	if (!ret_seg)
		goto ungrant_seg;

	memcpy(ret_seg, &seg->base, sizeof(struct dma_seg));

	ctx_res = (struct cdma_ctx_res *)dma_dev->private_data;
	ret = xa_err(xa_store(&ctx_res->seg_xa, ret_seg->handle, seg,
			      GFP_KERNEL));
	if (ret) {
		dev_err(cdev->dev, "store seg to ctx_res failed, ret = %d\n",
			ret);
		goto free_seg;
	}

	return ret_seg;

free_seg:
	kfree(ret_seg);
ungrant_seg:
	cdma_seg_ungrant(seg);
unregister_seg:
	cdma_unregister_seg(cdev, seg);
decrease_cnt:
	atomic_dec(&ctx->ref_cnt);
	return NULL;
}
EXPORT_SYMBOL_GPL(dma_register_seg);

void dma_unregister_seg(struct dma_device *dma_dev, struct dma_seg *dma_seg)
{
	struct cdma_ctx_res *ctx_res;
	struct cdma_context *ctx;
	struct cdma_segment *seg;
	struct cdma_dev *cdev;

	if (!dma_dev || !dma_dev->private_data || !dma_seg)
		return;

	cdev = get_cdma_dev_by_eid(dma_dev->attr.eid.dw0);
	if (!cdev) {
		pr_err("can not find cdev by eid, eid = 0x%x\n",
		       dma_dev->attr.eid.dw0);
		return;
	}

	ctx_res = (struct cdma_ctx_res *)dma_dev->private_data;
	seg = xa_load(&ctx_res->seg_xa, dma_seg->handle);
	if (!seg) {
		dev_err(cdev->dev,
			"no segment found in this device, handle = %llu\n",
			dma_seg->handle);
		return;
	}
	xa_erase(&ctx_res->seg_xa, dma_seg->handle);
	ctx = seg->ctx;

	cdma_seg_ungrant(seg);
	cdma_unregister_seg(cdev, seg);
	kfree(dma_seg);

	atomic_dec(&ctx->ref_cnt);
}
EXPORT_SYMBOL_GPL(dma_unregister_seg);

struct dma_seg *dma_import_seg(struct dma_seg_cfg *cfg)
{
	if (!cfg || !cfg->sva || !cfg->len)
		return NULL;

	return cdma_import_seg(cfg);
}
EXPORT_SYMBOL_GPL(dma_import_seg);

void dma_unimport_seg(struct dma_seg *dma_seg)
{
	if (!dma_seg)
		return;

	cdma_unimport_seg(dma_seg);
}
EXPORT_SYMBOL_GPL(dma_unimport_seg);

int dma_poll_queue(struct dma_device *dma_dev, int queue_id, u32 cr_cnt,
		   struct dma_cr *cr)
{
	struct cdma_queue *cdma_queue;
	struct cdma_dev *cdev;
	u32 eid;

	if (!dma_dev || !cr_cnt || !cr) {
		pr_err("the poll queue input parameter is invalid.\n");
		return -EINVAL;
	}

	eid = dma_dev->attr.eid.dw0;
	cdev = get_cdma_dev_by_eid(eid);
	if (!cdev) {
		pr_err("get cdma dev failed, eid = 0x%x.\n", eid);
		return -EINVAL;
	}

	if (cdev->status == CDMA_SUSPEND) {
		pr_warn("cdma device is not prepared, eid = 0x%x.\n", eid);
		return -EINVAL;
	}

	cdma_queue = cdma_find_queue(cdev, queue_id);
	if (!cdma_queue || !cdma_queue->jfc) {
		dev_err(cdev->dev, "get cdma queue failed, queue_id = %d.\n",
			queue_id);
		return -EINVAL;
	}

	return cdma_poll_jfc(cdma_queue->jfc, cr_cnt, cr);
}
EXPORT_SYMBOL_GPL(dma_poll_queue);

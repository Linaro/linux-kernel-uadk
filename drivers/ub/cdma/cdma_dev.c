// SPDX-License-Identifier: GPL-2.0+
/* Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved. */

#define dev_fmt(fmt) "CDMA: " fmt

#include <linux/auxiliary_bus.h>
#include <linux/ummu_core.h>
#include <linux/iommu.h>
#include <linux/dma-mapping.h>
#include <linux/bitmap.h>

#include "cdma.h"
#include "cdma_cmd.h"
#include "cdma_tid.h"
#include "cdma_context.h"
#include <ub/ubase/ubase_comm_ctrlq.h>
#include <ub/ubase/ubase_comm_dev.h>
#include "cdma_queue.h"
#include "cdma_dev.h"

static DEFINE_XARRAY(cdma_devs_tbl);
static atomic_t cdma_devs_num = ATOMIC_INIT(0);

struct cdma_dev *get_cdma_dev_by_eid(u32 eid)
{
	struct cdma_dev *cdev = NULL;
	unsigned long index = 0;

	xa_for_each(&cdma_devs_tbl, index, cdev)
		if (cdev->eid == eid)
			return cdev;

	return NULL;
}

struct xarray *get_cdma_dev_tbl(u32 *devs_num)
{
	*devs_num = atomic_read(&cdma_devs_num);

	return &cdma_devs_tbl;
}

/* Add the device to the device list for user query. */
static int cdma_add_device_to_list(struct cdma_dev *cdev)
{
	struct auxiliary_device *adev = cdev->adev;
	int ret;

	if (adev->id >= CDMA_UE_MAX_NUM) {
		dev_err(cdev->dev, "invalid ue id %u.\n", adev->id);
		return -EINVAL;
	}

	ret = xa_err(xa_store(&cdma_devs_tbl, adev->id, cdev, GFP_KERNEL));
	if (ret) {
		dev_err(cdev->dev,
			"store cdma device to table failed, adev id = %u.\n",
			adev->id);
		return ret;
	}

	atomic_inc(&cdma_devs_num);

	return 0;
}

static void cdma_del_device_from_list(struct cdma_dev *cdev)
{
	struct auxiliary_device *adev = cdev->adev;

	if (adev->id >= CDMA_UE_MAX_NUM) {
		dev_err(cdev->dev, "invalid ue id %u.\n", adev->id);
		return;
	}

	atomic_dec(&cdma_devs_num);
	xa_erase(&cdma_devs_tbl, adev->id);
}

static void cdma_tbl_init(struct cdma_table *table, u32 max, u32 min)
{
	if (!max || max < min)
		return;

	spin_lock_init(&table->lock);
	idr_init(&table->idr_tbl.idr);
	table->idr_tbl.max = max;
	table->idr_tbl.min = min;
	table->idr_tbl.next = min;
}

static void cdma_tbl_destroy(struct cdma_dev *cdev, struct cdma_table *table,
			     const char *table_name)
{
	if (!idr_is_empty(&table->idr_tbl.idr))
		dev_err(cdev->dev, "IDR not empty in clean up %s table.\n",
			table_name);
	idr_destroy(&table->idr_tbl.idr);
}

static void cdma_init_tables(struct cdma_dev *cdev)
{
	struct cdma_res *queue = &cdev->caps.queue;

	cdma_tbl_init(&cdev->queue_table, queue->start_idx + queue->max_cnt - 1,
		      queue->start_idx);
}

static void cdma_destroy_tables(struct cdma_dev *cdev)
{
	cdma_tbl_destroy(cdev, &cdev->queue_table, "QUEUE");
}

static void cdma_init_base_dev(struct cdma_dev *cdev)
{
	struct cdma_device_attr *attr = &cdev->base.attr;
	struct cdma_device_cap *dev_cap = &attr->dev_cap;
	struct cdma_caps *caps = &cdev->caps;

	attr->eid.dw0 = cdev->eid;
	dev_cap->max_jfc = caps->jfc.max_cnt;
	dev_cap->max_jfs = caps->jfs.max_cnt;
	dev_cap->max_jfc_depth = caps->jfc.depth;
	dev_cap->max_jfs_depth = caps->jfs.depth;
	dev_cap->trans_mode = caps->trans_mode;
	dev_cap->max_jfs_sge = caps->jfs_sge;
	dev_cap->max_jfs_rsge = caps->jfs_rsge;
	dev_cap->max_msg_size = caps->max_msg_len;
	dev_cap->ceq_cnt = caps->comp_vector_cnt;
	dev_cap->max_jfs_inline_len = caps->jfs_inline_sz;
}

static int cdma_init_dev_param(struct cdma_dev *cdev,
			       struct auxiliary_device *adev)
{
	struct ubase_resource_space *mem_base;
	int ret;

	mem_base = ubase_get_mem_base(adev);
	if (!mem_base)
		return -EINVAL;

	cdev->adev = adev;
	cdev->dev = adev->dev.parent;
	cdev->k_db_base = mem_base->addr;
	cdev->db_base = mem_base->addr_unmapped;

	ret = cdma_init_dev_caps(cdev);
	if (ret)
		return ret;

	cdma_init_base_dev(cdev);
	cdma_init_tables(cdev);

	dev_set_drvdata(&adev->dev, cdev);

	mutex_init(&cdev->db_mutex);
	mutex_init(&cdev->eu_mutex);
	INIT_LIST_HEAD(&cdev->db_page);
	mutex_init(&cdev->file_mutex);
	INIT_LIST_HEAD(&cdev->file_list);

	return 0;
}

static void cdma_uninit_dev_param(struct cdma_dev *cdev)
{
	mutex_destroy(&cdev->db_mutex);
	mutex_destroy(&cdev->eu_mutex);
	mutex_destroy(&cdev->file_mutex);
	dev_set_drvdata(&cdev->adev->dev, NULL);
	cdma_destroy_tables(cdev);
}

static void cdma_release_table_res(struct cdma_dev *cdev)
{
	struct cdma_queue *queue;
	int id;

	idr_for_each_entry(&cdev->queue_table.idr_tbl.idr, queue, id)
		cdma_delete_queue(cdev, queue->id);
}

static int cdma_ctrlq_eu_add(struct cdma_dev *cdev, struct eu_info *eu)
{
	struct cdma_device_attr *attr = &cdev->base.attr;
	struct eu_info *eus = cdev->base.attr.eus;
	u8 i;

	for (i = 0; i < attr->eu_num; i++) {
		if (eu->eid_idx != eus[i].eid_idx)
			continue;

		dev_dbg(cdev->dev,
			"cdma.%u: eid_idx[0x%x] eid[0x%x->0x%x] upi[0x%x->0x%x] update success.\n",
			cdev->adev->id, eu->eid_idx, eus[i].eid.dw0,
			eu->eid.dw0, eus[i].upi, eu->upi & CDMA_UPI_MASK);

		eus[i].eid = eu->eid;
		eus[i].upi = eu->upi & CDMA_UPI_MASK;

		if (attr->eu.eid_idx == eu->eid_idx) {
			attr->eu.eid = eu->eid;
			attr->eu.upi = eu->upi & CDMA_UPI_MASK;
		}
		return 0;
	}

	if (attr->eu_num >= CDMA_MAX_EU_NUM) {
		dev_err(cdev->dev, "cdma.%u: eu table is full.\n",
			cdev->adev->id);
		return -EINVAL;
	}

	eus[attr->eu_num++] = *eu;
	dev_dbg(cdev->dev,
		 "cdma.%u: eid_idx[0x%x] eid[0x%x] upi[0x%x] add success.\n",
		 cdev->adev->id, eu->eid_idx, eu->eid.dw0,
		 eu->upi & CDMA_UPI_MASK);

	return 0;
}

static int cdma_ctrlq_eu_del(struct cdma_dev *cdev, struct eu_info *eu)
{
	struct cdma_device_attr *attr = &cdev->base.attr;
	struct eu_info *eus = cdev->base.attr.eus;
	int ret = -EINVAL;
	u8 i, j;

	if (!attr->eu_num) {
		dev_err(cdev->dev, "cdma.%u: eu table is empty.\n",
			cdev->adev->id);
		return -EINVAL;
	}

	for (i = 0; i < attr->eu_num; i++) {
		if (eu->eid_idx != eus[i].eid_idx)
			continue;

		for (j = i; j < attr->eu_num - 1; j++)
			eus[j] = eus[j + 1];
		memset(&eus[j], 0, sizeof(*eus));

		if (attr->eu.eid_idx == eu->eid_idx)
			attr->eu = eus[0];
		attr->eu_num--;
		ret = 0;
		break;
	}

	dev_info(cdev->dev,
		 "cdma.%u: eid_idx[0x%x] eid[0x%x] upi[0x%x] delete %s.\n",
		 cdev->adev->id, eu->eid_idx, eu->eid.dw0,
		 eu->upi & CDMA_UPI_MASK, ret ? "failed" : "success");

	return ret;
}

static int cdma_ctrlq_eu_update(struct auxiliary_device *adev, u8 service_ver,
			 void *data, u16 len, u16 seq)
{
	struct cdma_dev *cdev = dev_get_drvdata(&adev->dev);
	struct cdma_ctrlq_eu_info *ctrlq_eu;
	int ret = -EINVAL;

	if (len < sizeof(*ctrlq_eu)) {
		dev_err(cdev->dev, "ctrlq data len is invalid.\n");
		return -EINVAL;
	}

	ctrlq_eu = (struct cdma_ctrlq_eu_info *)data;

	mutex_lock(&cdev->eu_mutex);
	if (ctrlq_eu->op == CDMA_CTRLQ_EU_ADD)
		ret = cdma_ctrlq_eu_add(cdev, &ctrlq_eu->eu);
	else if (ctrlq_eu->op == CDMA_CTRLQ_EU_DEL)
		ret = cdma_ctrlq_eu_del(cdev, &ctrlq_eu->eu);
	else
		dev_err(cdev->dev, "ctrlq eu op is invalid.\n");
	mutex_unlock(&cdev->eu_mutex);

	return ret;
}

int cdma_create_arm_db_page(struct cdma_dev *cdev)
{
	cdev->arm_db_page = alloc_page(GFP_KERNEL | __GFP_ZERO);
	if (!cdev->arm_db_page) {
		dev_err(cdev->dev, "alloc dev arm db page failed.\n");
		return -ENOMEM;
	}
	return 0;
}

void cdma_destroy_arm_db_page(struct cdma_dev *cdev)
{
	if (!cdev->arm_db_page)
		return;

	put_page(cdev->arm_db_page);
	cdev->arm_db_page = NULL;
}

int cdma_register_crq_event(struct auxiliary_device *adev)
{
	struct ubase_ctrlq_event_nb nb = {
		.service_type = UBASE_CTRLQ_SER_TYPE_DEV_REGISTER,
		.opcode = CDMA_CTRLQ_EU_UPDATE,
		.back = adev,
		.crq_handler = cdma_ctrlq_eu_update,
	};
	int ret;

	if (!adev)
		return -EINVAL;

	ret = ubase_ctrlq_register_crq_event(adev, &nb);
	if (ret) {
		dev_err(&adev->dev, "register crq event failed, id = %u, ret = %d.\n",
			adev->id, ret);
		return ret;
	}

	return 0;
}

void cdma_unregister_crq_event(struct auxiliary_device *adev)
{
	ubase_ctrlq_unregister_crq_event(adev,
					 UBASE_CTRLQ_SER_TYPE_DEV_REGISTER,
					 CDMA_CTRLQ_EU_UPDATE);
}

struct cdma_dev *cdma_create_dev(struct auxiliary_device *adev)
{
	struct cdma_dev *cdev;

	cdev = kzalloc((sizeof(*cdev)), GFP_KERNEL);
	if (!cdev)
		return NULL;

	if (cdma_init_dev_param(cdev, adev))
		goto free;

	if (cdma_add_device_to_list(cdev))
		goto free_param;

	if (cdma_alloc_dev_tid(cdev))
		goto del_list;

	if (cdma_register_crq_event(adev))
		goto free_tid;

	if (cdma_create_arm_db_page(cdev))
		goto unregister_crq;

	idr_init(&cdev->ctx_idr);
	spin_lock_init(&cdev->ctx_lock);

	dev_dbg(&adev->dev, "cdma.%u init succeeded.\n", adev->id);

	return cdev;

unregister_crq:
	cdma_unregister_crq_event(adev);
free_tid:
	cdma_free_dev_tid(cdev);
del_list:
	cdma_del_device_from_list(cdev);
free_param:
	cdma_uninit_dev_param(cdev);
free:
	kfree(cdev);
	return NULL;
}

void cdma_destroy_dev(struct cdma_dev *cdev)
{
	struct cdma_context *tmp;
	int id;

	if (!cdev)
		return;

	ubase_virt_unregister(cdev->adev);

	cdma_release_table_res(cdev);

	idr_for_each_entry(&cdev->ctx_idr, tmp, id)
		cdma_free_context(cdev, tmp);
	idr_destroy(&cdev->ctx_idr);

	cdma_destroy_arm_db_page(cdev);
	ubase_ctrlq_unregister_crq_event(cdev->adev,
					 UBASE_CTRLQ_SER_TYPE_DEV_REGISTER,
					 CDMA_CTRLQ_EU_UPDATE);
	cdma_free_dev_tid(cdev);

	cdma_del_device_from_list(cdev);
	cdma_uninit_dev_param(cdev);
	kfree(cdev);
}

bool cdma_find_seid_in_eus(struct eu_info *eus, u8 eu_num, struct dev_eid *eid,
			   struct eu_info *eu_out)
{
	u32 i;

	for (i = 0; i < eu_num; i++)
		if (eus[i].eid.dw0 == eid->dw0 && eus[i].eid.dw1 == eid->dw1 &&
		    eus[i].eid.dw2 == eid->dw2 && eus[i].eid.dw3 == eid->dw3) {
			*eu_out = eus[i];
			return true;
		}

	return false;
}

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
#include <ub/ubase/ubase_comm_ctrlq.h>
#include <ub/ubase/ubase_comm_dev.h>
#include "cdma_dev.h"

static DEFINE_XARRAY(cdma_devs_tbl);
static atomic_t cdma_devs_num = ATOMIC_INIT(0);

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

	if (cdma_create_arm_db_page(cdev))
		goto free_tid;

	dev_dbg(&adev->dev, "cdma.%u init succeeded.\n", adev->id);

	return cdev;

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
	if (!cdev)
		return;

	cdma_destroy_arm_db_page(cdev);
	ubase_ctrlq_unregister_crq_event(cdev->adev,
					 UBASE_CTRLQ_SER_TYPE_DEV_REGISTER,
					 CDMA_CTRLQ_EU_UPDATE);
	cdma_free_dev_tid(cdev);

	cdma_del_device_from_list(cdev);
	cdma_uninit_dev_param(cdev);
	kfree(cdev);
}

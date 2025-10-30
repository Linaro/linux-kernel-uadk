// SPDX-License-Identifier: GPL-2.0+
/* Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved. */

#define dev_fmt(fmt) "CDMA: " fmt

#include <linux/iommu.h>
#include <linux/ummu_core.h>

#include "cdma.h"
#include "cdma_tid.h"

int cdma_alloc_dev_tid(struct cdma_dev *cdev)
{
	struct ummu_seg_attr seg_attr = {
		.token = NULL,
		.e_bit = UMMU_EBIT_ON,
	};
	struct ummu_param drvdata = {
		.mode = MAPT_MODE_TABLE,
	};
	int ret;

	ret = iommu_dev_enable_feature(cdev->dev, IOMMU_DEV_FEAT_KSVA);
	if (ret) {
		dev_err(cdev->dev, "enable ksva failed, ret = %d.\n", ret);
		return ret;
	}

	ret = iommu_dev_enable_feature(cdev->dev, IOMMU_DEV_FEAT_SVA);
	if (ret) {
		dev_err(cdev->dev, "enable sva failed, ret = %d.\n", ret);
		goto err_sva_enable_dev;
	}

	cdev->ksva = ummu_ksva_bind_device(cdev->dev, &drvdata);
	if (!cdev->ksva) {
		dev_err(cdev->dev, "ksva bind device failed.\n");
		ret = -EINVAL;
		goto err_ksva_bind_device;
	}

	ret = ummu_get_tid(cdev->dev, cdev->ksva, &cdev->tid);
	if (ret) {
		dev_err(cdev->dev, "get tid for cdma device failed.\n");
		goto err_get_tid;
	}

	ret = ummu_sva_grant_range(cdev->ksva, 0, CDMA_MAX_GRANT_SIZE,
				   UMMU_DEV_WRITE | UMMU_DEV_READ,
				   &seg_attr);
	if (ret) {
		dev_err(cdev->dev, "sva grant range for cdma device failed.\n");
		goto err_get_tid;
	}

	return 0;

err_get_tid:
	ummu_ksva_unbind_device(cdev->ksva);
err_ksva_bind_device:
	if (iommu_dev_disable_feature(cdev->dev, IOMMU_DEV_FEAT_SVA))
		dev_warn(cdev->dev, "disable sva failed, ret = %d.\n", ret);
err_sva_enable_dev:
	if (iommu_dev_disable_feature(cdev->dev, IOMMU_DEV_FEAT_KSVA))
		dev_warn(cdev->dev, "disable ksva failed, ret = %d.\n", ret);

	return ret;
}

void cdma_free_dev_tid(struct cdma_dev *cdev)
{
	int ret;

	ret = ummu_sva_ungrant_range(cdev->ksva, 0, CDMA_MAX_GRANT_SIZE, NULL);
	if (ret)
		dev_warn(cdev->dev,
			 "sva ungrant range for cdma device failed, ret = %d.\n",
			 ret);

	ummu_ksva_unbind_device(cdev->ksva);

	ret = iommu_dev_disable_feature(cdev->dev, IOMMU_DEV_FEAT_SVA);
	if (ret)
		dev_warn(cdev->dev, "disable sva failed, ret = %d.\n", ret);

	ret = iommu_dev_disable_feature(cdev->dev, IOMMU_DEV_FEAT_KSVA);
	if (ret)
		dev_warn(cdev->dev, "disable ksva failed, ret = %d.\n", ret);
}

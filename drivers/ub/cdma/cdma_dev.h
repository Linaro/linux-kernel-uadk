/* SPDX-License-Identifier: GPL-2.0+ */
/* Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved. */

#ifndef __CDMA_DEV_H__
#define __CDMA_DEV_H__

#include <linux/auxiliary_bus.h>

#define CDMA_CTRLQ_EU_UPDATE 0x2
#define CDMA_UE_MAX_NUM 64

struct cdma_dev;

struct cdma_dev *cdma_create_dev(struct auxiliary_device *adev);
void cdma_destroy_dev(struct cdma_dev *cdev);
int cdma_create_arm_db_page(struct cdma_dev *cdev);
void cdma_destroy_arm_db_page(struct cdma_dev *cdev);

#endif /* _CDMA_DEV_H_ */

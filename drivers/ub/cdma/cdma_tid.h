/* SPDX-License-Identifier: GPL-2.0+ */
/* Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved. */

#ifndef __CDMA_TID_H__
#define __CDMA_TID_H__

#include <linux/bits.h>

#define CDMA_MAX_GRANT_SIZE GENMASK(47, 12)

struct cdma_dev;

int cdma_alloc_dev_tid(struct cdma_dev *cdev);
void cdma_free_dev_tid(struct cdma_dev *cdev);

#endif

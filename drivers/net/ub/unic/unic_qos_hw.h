/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved.
 *
 */

#ifndef __UNIC_QOS_HW_H__
#define __UNIC_QOS_HW_H__

#include <linux/ethtool.h>

#include "unic_dev.h"

int unic_set_hw_vl_map(struct unic_dev *unic_dev, u8 *dscp_vl, u8 *prio_vl,
		       u8 map_type);
int unic_config_vl_rate_limit(struct unic_dev *unic_dev, u64 *vl_maxrate,
			      u16 vl_bitmap);

#endif

/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved.
 * Description: Hisilicon UMMU config table implementation v1
 */

#ifndef __UMMU_CFG_V1_H__
#define __UMMU_CFG_V1_H__

#include "ummu.h"

struct hisi_ummu_tdev_info {
	int version;
	union {
		struct {
			u64 ummu_idx_mask;
			bool on_chip;
		} v1;
	};
};

void ummu_cfg_impl_init(struct ummu_device *ummu);

#endif /* __UMMU_CFG_V1_H__ */

// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved.
 * Description: UMMU implementation glue layer
 */

#include "ummu_impl.h"
#include "ummu_cfg_v1.h"
#include "ubmem-mmu/ubmem_mmu.h"

static struct ummu_dev_impl_ops ummu_impl_ops;

struct ummu_device *ummu_impl_init(struct ummu_device *ummu)
{
	ummu->impl_ops = &ummu_impl_ops;

	ummu_cfg_impl_init(ummu);
	if (IS_ENABLED(CONFIG_UB_UBMEM_UMMU))
		ummu = ubmem_mmu_impl_init(ummu);

	return ummu;
}

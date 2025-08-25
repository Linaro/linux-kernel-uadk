/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved.
 * Description: UBMEM-MMU API
 */
#ifndef __UBMEM_MMU_H__
#define __UBMEM_MMU_H__

#include "../ummu.h"

#if IS_ENABLED(CONFIG_UB_UBMEM_UMMU)
struct ummu_device *ubmem_mmu_impl_init(struct ummu_device *ummu);
#else
static inline struct ummu_device *ubmem_mmu_impl_init(struct ummu_device *ummu)
{
	return ERR_PTR(-ENODEV);
}
#endif
#endif   /* __UBMEM_MMU_H__ */

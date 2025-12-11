/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved.
 * Description: UMMU IOMMU Interface
 */

#ifndef __UMMU_IOMMU_H__
#define __UMMU_IOMMU_H__

#include "ummu.h"

extern struct iommu_ops ummu_iommu_ops;

struct ummu_domain *ummu_domain_alloc_helper(void);
#endif /* __UMMU_IOMMU_H__ */

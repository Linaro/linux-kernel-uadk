/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved.
 * Description: UMMU Framework's implementations.
 */

#ifndef __LOGIC_UMMU_H__
#define __LOGIC_UMMU_H__
#include <linux/ummu_core.h>
#include "../ummu.h"

struct iommu_domain *iommu_to_agent_domain(struct iommu_domain *dom);
int logic_add_ummu_device(struct ummu_device *ummu,
			  const struct iommu_ops *iommu_ops,
			  const struct ummu_core_ops *core_ops);
void logic_remove_ummu_device(struct ummu_device *ummu);
int logic_ummu_device_init(void);
void logic_ummu_device_exit(void);

#endif  /* __LOGIC_UMMU_H__ */

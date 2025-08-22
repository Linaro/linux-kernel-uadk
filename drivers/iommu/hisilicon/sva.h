/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved.
 * Description: Implementation of the IOMMU SVA API for the UMMU
 */

#ifndef __UMMU_SVA_H__
#define __UMMU_SVA_H__

#include "ummu.h"

#if IS_ENABLED(CONFIG_UB_UMMU_SVA)
int ummu_master_enable_sva(struct ummu_master *master,
			   enum iommu_dev_features feat);
int ummu_master_disable_sva(struct ummu_master *master,
			    enum iommu_dev_features feat);
struct iommu_domain *ummu_domain_alloc_sva(struct device *dev,
					   struct mm_struct *mm);
void ummu_sva_domain_remove_tid(struct ummu_domain *domain,
				struct ummu_master *master,
				u32 tid);
bool ummu_sva_supported(struct ummu_device *ummu);
void ummu_sva_tcte_invalidate(struct ummu_domain *u_domain);
#else /* CONFIG_UB_UMMU_SVA */
static inline int ummu_master_enable_sva(struct ummu_master *master,
					 enum iommu_dev_features feat)
{
	return -ENODEV;
}

static inline int ummu_master_disable_sva(struct ummu_master *master,
					  enum iommu_dev_features feat)
{
	return -ENODEV;
}

static inline void ummu_sva_domain_remove_tid(struct ummu_domain *domain,
					      struct ummu_master *master,
					      u32 tid)
{
}

#define ummu_domain_alloc_sva ((void *) 0)

static inline bool ummu_sva_supported(struct ummu_device *ummu)
{
	return false;
}

static inline void ummu_sva_tcte_invalidate(struct ummu_domain *u_domain)
{
}
#endif /* CONFIG_UB_UMMU_SVA */

#endif /* __UMMU_SVA_H__ */

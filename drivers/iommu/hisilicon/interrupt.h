/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved.
 * Description: UMMU Interrupt interface
 */

#ifndef __UMMU_INTERRUPT_H__
#define __UMMU_INTERRUPT_H__

#include "ummu.h"

void ummu_setup_irqs(struct ummu_device *ummu);
void ummu_page_response(struct device *dev, struct iopf_fault *evt,
			struct iommu_page_response *resp);

#endif /* __UMMU_INTERRUPT_H__ */

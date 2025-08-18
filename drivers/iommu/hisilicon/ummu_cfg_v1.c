// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved.
 * Description: Hisilicon UMMU config table implementation v1
 */

#include <linux/iopoll.h>

#include "regs.h"
#include "queue.h"
#include "ummu_cfg_v1.h"

/* USER LOGIC DEFINE */
#define UMMU_USER_LOGIC_BASE 0x3000

#define UMMU_UMCMD_PAGE_SEL_OFFSET 0x3834
#define PAGE_MODE_SEL_EN (1UL << 0)
#define MAPT_CTRL_PAGE_SIZE PAGE_SIZE
#define MAPT_QUEUE_NUM_16K 0x4000
#define MAPT_QUEUE_NUM_64K 0x10000

#define UMMU_USER_CONFIG2_OFFSET 0x4c08
#define MCMDQ_MEM_INITING (1UL << 4)
#define MCMDQ_MEM_INIT_EN (1UL << 3)

static void ummu_cfg_probe_premq_cap(struct ummu_device *ummu)
{
	void __iomem *base_addr = ummu->base + UMMU_UMCMD_PAGE_SEL_OFFSET;
	u32 reg = readl_relaxed(base_addr);

	/* the total space size of the MAPT queue ctrl page is fixed. */
	if (MAPT_CTRL_PAGE_SIZE == SZ_4K) {
		reg |= PAGE_MODE_SEL_EN;
		ummu->cap.permq_num = MAPT_QUEUE_NUM_64K;
	} else {
		reg &= ~PAGE_MODE_SEL_EN;
		ummu->cap.permq_num = MAPT_QUEUE_NUM_16K;
	}

	writel_relaxed(reg, base_addr);
}

static int ummu_cfg_hw_probe(struct ummu_device *ummu)
{
	ummu_cfg_probe_premq_cap(ummu);

	return 0;
}

static int ummu_cfg_mcmdq_cfg(struct ummu_device *ummu)
{
	void __iomem *base_addr = ummu->base + UMMU_USER_CONFIG2_OFFSET;
	u32 reg = readl_relaxed(base_addr);
	int ret;

	reg |= MCMDQ_MEM_INIT_EN;
	writel_relaxed(reg, base_addr);
	ret = readl_relaxed_poll_timeout(base_addr, reg,
					 (reg & MCMDQ_MEM_INITING) == 0, 1,
					 UMMU_REG_POLL_TIMEOUT_US);
	if (ret) {
		dev_err(ummu->dev,
			 "user_config2 responding timeout ret = %d.\n", ret);
		return ret;
	}

	reg &= ~MCMDQ_MEM_INIT_EN;
	writel_relaxed(reg, base_addr);

	return ret;
}

void ummu_cfg_impl_init(struct ummu_device *ummu)
{
	ummu->impl_ops->hw_probe = ummu_cfg_hw_probe;
	ummu->impl_ops->mcmdq_cfg = ummu_cfg_mcmdq_cfg;
}

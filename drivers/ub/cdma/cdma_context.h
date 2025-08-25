/* SPDX-License-Identifier: GPL-2.0+ */
/* Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved. */

#ifndef __CDMA_CONTEXT_H__
#define __CDMA_CONTEXT_H__

#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <ub/cdma/cdma_api.h>

struct cdma_context {
	struct dma_context base_ctx;
	struct cdma_dev *cdev;
	struct iommu_sva *sva;
	struct list_head pgdir_list;
	struct mutex pgdir_mutex;
	spinlock_t lock;
	int handle;
	u32 tid;
	bool is_kernel;
	atomic_t ref_cnt;
};

struct cdma_ctx_res {
	struct cdma_context *ctx;
};

#endif /* CDMA_CONTEXT_H */

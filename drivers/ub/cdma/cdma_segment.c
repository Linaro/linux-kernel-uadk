// SPDX-License-Identifier: GPL-2.0+
/* Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved. */

#define dev_fmt(fmt) "CDMA: " fmt

#include <linux/ummu_core.h>
#include "cdma_segment.h"

static inline void cdma_free_seg_handle(struct cdma_dev *cdev, u64 handle)
{
	spin_lock(&cdev->seg_table.lock);
	idr_remove(&cdev->seg_table.idr_tbl.idr, handle);
	spin_unlock(&cdev->seg_table.lock);
}

void cdma_unregister_seg(struct cdma_dev *cdev, struct cdma_segment *seg)
{
	cdma_free_seg_handle(cdev, seg->base.handle);
	cdma_umem_release(seg->umem, seg->is_kernel);
	kfree(seg);
}

void cdma_seg_ungrant(struct cdma_segment *seg)
{
	struct ummu_token_info token_info = { 0 };

	token_info.tokenVal = seg->base.token_value;

	ummu_sva_ungrant_range(seg->ksva, (void *)seg->base.sva,
				     seg->base.len, &token_info);
}

struct dma_seg *cdma_import_seg(struct dma_seg_cfg *cfg)
{
	struct dma_seg *seg;

	seg = kzalloc(sizeof(*seg), GFP_KERNEL);
	if (!seg)
		return NULL;

	seg->sva = cfg->sva;
	seg->len = cfg->len;
	seg->token_value = cfg->token_value;

	return seg;
}

void cdma_unimport_seg(struct dma_seg *seg)
{
	kfree(seg);
}

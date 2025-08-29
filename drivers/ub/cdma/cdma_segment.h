/* SPDX-License-Identifier: GPL-2.0+ */
/* Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved. */

#ifndef __CDMA_SEGMENT_H__
#define __CDMA_SEGMENT_H__

#include "cdma_common.h"
#include <ub/cdma/cdma_api.h>

struct cdma_dev;

struct cdma_segment {
	struct dma_seg base;
	struct iommu_sva *ksva;
	struct cdma_umem *umem;
	struct cdma_context *ctx;
	bool is_kernel;
	struct list_head list;
};

static inline struct cdma_segment *to_cdma_seg(struct dma_seg *seg)
{
	return container_of(seg, struct cdma_segment, base);
}

struct cdma_segment *cdma_register_seg(struct cdma_dev *cdev,
				       struct dma_seg_cfg *cfg, bool is_kernel);
void cdma_unregister_seg(struct cdma_dev *cdev, struct cdma_segment *seg);
int cdma_seg_grant(struct cdma_dev *cdev, struct cdma_segment *seg,
		   struct dma_seg_cfg *cfg);
void cdma_seg_ungrant(struct cdma_segment *seg);
struct dma_seg *cdma_import_seg(struct dma_seg_cfg *cfg);
void cdma_unimport_seg(struct dma_seg *seg);

#endif /* CDMA_SEGMENT_H */

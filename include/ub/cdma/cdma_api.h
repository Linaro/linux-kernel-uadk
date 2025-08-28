/* SPDX-License-Identifier: GPL-2.0+ */
/* Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved. */

#ifndef _UB_CDMA_CDMA_API_H_
#define _UB_CDMA_CDMA_API_H_

#include <linux/atomic.h>
#include <uapi/ub/cdma/cdma_abi.h>

struct dma_device {
	struct cdma_device_attr attr;
	atomic_t ref_cnt;
	void *private_data;
};

struct queue_cfg {
	u32 queue_depth;
	u8 priority;
	u64 user_ctx;
	u32 dcna;
	struct dev_eid rmt_eid;
	u32 trans_mode;
};

struct dma_context {
	struct dma_device *dma_dev;
	u32 tid; /* data valid only in bit 0-19 */
};

struct dma_device *dma_get_device_list(u32 *num_devices);

void dma_free_device_list(struct dma_device *dev_list, u32 num_devices);

struct dma_device *dma_get_device_by_eid(struct dev_eid *eid);

int dma_create_context(struct dma_device *dma_dev);

int dma_alloc_queue(struct dma_device *dma_dev, int ctx_id,
		    struct queue_cfg *cfg);

#endif

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

struct dma_context {
	struct dma_device *dma_dev;
	u32 tid; /* data valid only in bit 0-19 */
};

struct dma_device *dma_get_device_list(u32 *num_devices);

void dma_free_device_list(struct dma_device *dev_list, u32 num_devices);

#endif

/* SPDX-License-Identifier: GPL-2.0+ */
/* Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved. */

#ifndef __CDMA_COMMON_H__
#define __CDMA_COMMON_H__

#include <linux/types.h>
#include "cdma.h"

#define CDMA_DB_SIZE 64

struct cdma_umem_param {
	struct cdma_dev *dev;
	u64 va;
	u64 len;
	union cdma_umem_flag flag;
	bool is_kernel;
};

static inline u64 cdma_cal_npages(u64 va, u64 len)
{
	return (ALIGN(va + len, PAGE_SIZE) - ALIGN_DOWN(va, PAGE_SIZE)) /
		PAGE_SIZE;
}

struct cdma_umem *cdma_umem_get(struct cdma_dev *cdev, u64 va, u64 len,
				bool is_kernel);
void cdma_umem_release(struct cdma_umem *umem, bool is_kernel);

int cdma_k_alloc_buf(struct cdma_dev *cdev, size_t memory_size,
		     struct cdma_buf *buf);
void cdma_k_free_buf(struct cdma_dev *cdev, size_t memory_size,
		     struct cdma_buf *buf);
int cdma_pin_queue_addr(struct cdma_dev *cdev, u64 addr, u32 len,
			struct cdma_buf *buf);
void cdma_unpin_queue_addr(struct cdma_umem *umem);

#endif

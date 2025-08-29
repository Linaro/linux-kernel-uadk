/* SPDX-License-Identifier: GPL-2.0+ */
/* Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved. */

#ifndef __CDMA_COMMON_H__
#define __CDMA_COMMON_H__

#include <linux/types.h>

struct cdma_umem;
struct cdma_dev;
struct cdma_buf;

void cdma_umem_release(struct cdma_umem *umem, bool is_kernel);

void cdma_k_free_buf(struct cdma_dev *cdev, size_t memory_size,
		     struct cdma_buf *buf);

void cdma_unpin_queue_addr(struct cdma_umem *umem);

#endif

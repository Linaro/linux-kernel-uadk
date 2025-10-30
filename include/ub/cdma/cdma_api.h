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

#endif

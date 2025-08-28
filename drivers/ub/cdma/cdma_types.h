/* SPDX-License-Identifier: GPL-2.0+ */
/* Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved. */

#ifndef __CDMA_TYPES_H__
#define __CDMA_TYPES_H__

#include <linux/list.h>
#include <linux/idr.h>
#include <linux/spinlock.h>

struct cdma_dev;

struct cdma_file {
	struct cdma_dev *cdev;
	struct list_head list;
	struct mutex ctx_mutex;
	struct cdma_context *uctx;
	struct idr idr;
	spinlock_t idr_lock;
	struct kref ref;
};

#endif

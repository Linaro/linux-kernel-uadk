/* SPDX-License-Identifier: GPL-2.0+ */
/* Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved. */

#ifndef __CDMA_TYPES_H__
#define __CDMA_TYPES_H__

#include <linux/list.h>
#include <linux/idr.h>
#include <linux/spinlock.h>

struct cdma_dev;

struct cdma_ucontext {
	struct cdma_dev *dev;
	u32 eid;
	u32 eid_index;
};

struct cdma_udrv_priv {
	u64 in_addr;
	u32 in_len;
	u64 out_addr;
	u32 out_len;
};

struct cdma_tp_cfg {
	u32 scna;
	u32 dcna;
	u32 seid;
	u32 deid;
};

struct cdma_base_tp {
	struct cdma_ucontext *uctx;
	struct cdma_tp_cfg cfg;
	u64 usr_tp;
	u32 tpn;
	u32 tp_id;
};

struct cdma_udata {
	struct cdma_context *uctx;
	struct cdma_udrv_priv *udrv_data;
};

struct cdma_jfc_cfg {
	u32 depth;
	u32 ceqn;
	u32 queue_id;
};

struct cdma_base_jfc {
	struct cdma_dev *dev;
	struct cdma_context *ctx;
	struct cdma_jfc_cfg jfc_cfg;
	u32 id;
	struct hlist_node hnode;
	atomic_t use_cnt;
};

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

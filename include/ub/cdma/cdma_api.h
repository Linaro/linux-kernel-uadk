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

enum dma_cr_opcode {
	DMA_CR_OPC_SEND = 0x00,
	DMA_CR_OPC_SEND_WITH_IMM,
	DMA_CR_OPC_SEND_WITH_INV,
	DMA_CR_OPC_WRITE_WITH_IMM,
};

union dma_cr_flag {
	struct {
		u8 s_r : 1;
		u8 jetty : 1;
		u8 suspend_done : 1;
		u8 flush_err_done : 1;
		u8 reserved : 4;
	} bs;
	u8 value;
};

struct dma_cr {
	enum dma_cr_status status;
	u64 user_ctx;
	enum dma_cr_opcode opcode;
	union dma_cr_flag flag;
	u32 completion_len;
	u32 local_id;
	u32 remote_id;
	u32 tpn;
};

struct queue_cfg {
	u32 queue_depth;
	u8 priority;
	u64 user_ctx;
	u32 dcna;
	struct dev_eid rmt_eid;
	u32 trans_mode;
};

struct dma_seg {
	u64 handle;
	u64 sva;
	u64 len;
	u32 tid; /* data valid only in bit 0-19 */
	u32 token_value;
	bool token_value_valid;
};

struct dma_seg_cfg {
	u64 sva;
	u64 len;
	u32 token_value;
	bool token_value_valid;
};

struct dma_context {
	struct dma_device *dma_dev;
	u32 tid; /* data valid only in bit 0-19 */
};

struct dma_device *dma_get_device_list(u32 *num_devices);

void dma_free_device_list(struct dma_device *dev_list, u32 num_devices);

struct dma_device *dma_get_device_by_eid(struct dev_eid *eid);

int dma_create_context(struct dma_device *dma_dev);

void dma_delete_context(struct dma_device *dma_dev, int handle);

int dma_alloc_queue(struct dma_device *dma_dev, int ctx_id,
		    struct queue_cfg *cfg);

void dma_free_queue(struct dma_device *dma_dev, int queue_id);

void dma_unregister_seg(struct dma_device *dma_dev, struct dma_seg *dma_seg);

struct dma_seg *dma_import_seg(struct dma_seg_cfg *cfg);

void dma_unimport_seg(struct dma_seg *dma_seg);

int dma_poll_queue(struct dma_device *dma_dev, int queue_id, u32 cr_cnt,
		   struct dma_cr *cr);

#endif

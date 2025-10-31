/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved.
 *
 */

#ifndef _UB_UBASE_COMM_HW_H_
#define _UB_UBASE_COMM_HW_H_

#include <linux/types.h>
#include <ub/ubase/ubase_comm_dev.h>

#define UBASE_DESC_DATA_LEN		6
struct ubase_cmdq_desc {
	__le16 opcode;
	u8 flag;
	u8 bd_num;
	__le16 ret;
	__le16 rsv;
	__le32 data[UBASE_DESC_DATA_LEN];
};

struct ubase_cmdq_ring {
	u32 ci;
	u32 pi;
	u32 desc_num;
	u32 tx_timeout;
	dma_addr_t desc_dma_addr;
	struct ubase_cmdq_desc *desc;
	spinlock_t lock;
};

struct ubase_cmdq {
	struct ubase_cmdq_ring csq;
	struct ubase_cmdq_ring crq;
};

struct ubase_hw {
	struct ubase_resource_space rs0_base;
	struct ubase_resource_space io_base;
	struct ubase_resource_space mem_base;
	struct ubase_cmdq cmdq;
	unsigned long state;
};

struct ubase_mbx_event_context {
	struct completion	done;
	int			result;
	u64			out_param;
	u16			seq_num;
};

#endif

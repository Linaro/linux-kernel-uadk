// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) HiSilicon Technologies Co., Ltd. 2025. All rights reserved.
 */

#include <linux/dma-mapping.h>

#include "hisi-msg.h"

#define HI_MSG_RQE_SIZE_128 0x80
#define hi_msg_rqe_size_hw(sz_sw) (ilog2((sz_sw) / (HI_MSG_RQE_SIZE_128)))

struct hi_msgq_reg {
	u32 pi;
	u32 ci;
	u32 depth;
	u32 entry_size;
	u32 addr_l;
	u32 addr_h;
};

static struct hi_msgq_reg msgq_reg[MSGQ_NUM] = {
	{ SQ_PI, SQ_CI, SQ_DEPTH, 0, SQ_ADDR_L, SQ_ADDR_H },
	{ RQ_PI, RQ_CI, RQ_DEPTH, RQ_ENTRY_SIZE, RQ_ADDR_L, RQ_ADDR_H },
	{ CQ_PI, CQ_CI, CQ_DEPTH, 0, CQ_ADDR_L, CQ_ADDR_H }
};

static const char * const msgq_user_name[] = { "UBUS" };

struct hi_msgq_cfg {
	u32 depth;
	u32 entry_size;
};

static int hi_msg_queue_sw_init(struct hi_msg_core *hmc, int user,
				int idx)
{
	struct hi_msgq_cfg msgq_cfg[MSGQ_USER_NUMS][MSGQ_NUM] = {
		{ { HI_SQ_CFG_DEPTH, HI_MSG_SQE_SIZE },
		  { HI_RQ_CFG_DEPTH, HI_MSG_RQE_SIZE },
		  { HI_CQ_CFG_DEPTH, HI_MSG_CQE_SIZE } }
	};
	struct hi_msg_queue *q = &hmc->queue[idx];
	struct device *dev = hmc->dev;

	q->ci = 0;
	q->pi = 0;
	q->depth = msgq_cfg[user][idx].depth;
	q->entry_size = msgq_cfg[user][idx].entry_size;
	q->total_size = q->depth * q->entry_size;
	spin_lock_init(&q->lock);

	if (idx == MSG_SQ)
		q->total_size += HI_MSG_SQE_PLD_SIZE * q->depth;

	q->entry = dma_alloc_coherent(dev, q->total_size, &q->dma_addr,
				      GFP_KERNEL);
	if (!q->entry)
		return -ENOMEM;

	dev_info(dev, "%s queue=%d, ci=%#x, pi=%#x, depth=%#x, entry_size=%#x, size=%#lx\n",
		 msgq_user_name[user], idx, q->ci, q->pi, q->depth,
		 q->entry_size, q->total_size);

	return 0;
}

static void hi_msg_queue_sw_uninit(struct hi_msg_core *hmc, int idx)
{
	struct hi_msg_queue *q = &hmc->queue[idx];

	dma_free_coherent(hmc->dev, q->total_size, q->entry, q->dma_addr);
}

u32 hi_msg_reg_read(struct hi_msg_core *hmc, u16 offset)
{
	return readl(hmc->reg_base + offset);
}

void hi_msg_reg_write(struct hi_msg_core *hmc, u16 offset, u32 val)
{
	writel(val, hmc->reg_base + offset);
}

static void hi_msg_queue_hw_init(struct hi_msg_core *hmc, int idx)
{
	struct hi_msg_queue *q = &hmc->queue[idx];

	/* cfg depth pre */
	hi_msg_reg_write(hmc, msgq_reg[idx].depth, q->depth);

	if (idx == MSG_SQ)
		hi_msg_reg_write(hmc, msgq_reg[idx].pi, q->pi);
	else
		hi_msg_reg_write(hmc, msgq_reg[idx].ci, q->ci);

	hi_msg_reg_write(hmc, msgq_reg[idx].addr_l, q->dma_addr & 0xffffffff);
	hi_msg_reg_write(hmc, msgq_reg[idx].addr_h,
			 q->dma_addr >> BITS_PER_TYPE(u32));

	if (idx == MSG_RQ)
		hi_msg_reg_write(hmc, msgq_reg[idx].entry_size,
				 hi_msg_rqe_size_hw(HI_MSG_RQE_SIZE));
}

static void hi_msg_reset_queue(struct hi_msg_core *hmc)
{
	hi_msg_reg_write(hmc, MSGQ_RST, 1);
	hi_msg_reg_write(hmc, MSGQ_RST, 0);
}

int hi_msg_core_init(struct hi_msg_core *hmc, int user)
{
	int i, j, ret;

	hmc->user = user;
	hmc->reg_base = ioremap(hmc->q_addr, hmc->q_size);
	if (!hmc->reg_base)
		return -ENOMEM;

	for (i = 0; i < MSGQ_NUM; i++) {
		ret = hi_msg_queue_sw_init(hmc, user, i);
		if (ret)
			goto sw_uninit;
	}

	hi_msg_reset_queue(hmc);

	for (i = 0; i < MSGQ_NUM; i++)
		hi_msg_queue_hw_init(hmc, i);

	return 0;
sw_uninit:
	hi_msg_reset_queue(hmc);

	for (j = 0; j < i; j++)
		hi_msg_queue_sw_uninit(hmc, j);

	iounmap(hmc->reg_base);
	return ret;
}

void hi_msg_core_uninit(struct hi_msg_core *hmc)
{
	int i;

	hi_msg_reset_queue(hmc);

	for (i = 0; i < MSGQ_NUM; i++)
		hi_msg_queue_sw_uninit(hmc, i);

	iounmap(hmc->reg_base);
}

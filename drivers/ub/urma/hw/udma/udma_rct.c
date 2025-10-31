// SPDX-License-Identifier: GPL-2.0+
/* Copyright(c) 2025 HiSilicon Technologies CO., Ltd. All rights reserved. */

#define dev_fmt(fmt) "UDMA: " fmt

#include <linux/dma-mapping.h>
#include "udma_cmd.h"
#include "udma_rct.h"

static int udma_create_rc_queue_ctx(struct udma_dev *dev, struct udma_rc_queue *rcq)
{
	struct ubase_mbx_attr attr = {};
	struct udma_rc_ctx ctx = {};

	ctx.type = RC_TYPE;
	ctx.state = RC_READY_STATE;
	ctx.rce_token_id_l = dev->tid & (uint32_t)RCE_TOKEN_ID_L_MASK;
	ctx.rce_token_id_h = dev->tid >> RCE_TOKEN_ID_H_OFFSET;
	ctx.rce_base_addr_l = (rcq->buf.addr >> RCE_ADDR_L_OFFSET) &
			       (uint32_t)RCE_ADDR_L_MASK;
	ctx.rce_base_addr_h = rcq->buf.addr >> RCE_ADDR_H_OFFSET;
	ctx.rce_shift = ilog2(roundup_pow_of_two(rcq->buf.entry_cnt));
	ctx.avail_sgmt_ost = RC_AVAIL_SGMT_OST;

	attr.tag = rcq->id;
	attr.op = UDMA_CMD_CREATE_RC_CONTEXT;

	return post_mailbox_update_ctx(dev, &ctx, sizeof(ctx), &attr);
}

static int udma_destroy_rc_queue_ctx(struct udma_dev *dev, struct udma_rc_queue *rcq)
{
	struct ubase_mbx_attr mbox_attr = {};
	struct ubase_cmd_mailbox *mailbox;
	int ret;

	mailbox = udma_alloc_cmd_mailbox(dev);
	if (!mailbox) {
		dev_err(dev->dev, "failed to alloc mailbox for rc queue.\n");
		return -ENOMEM;
	}

	mbox_attr.tag = rcq->id;
	mbox_attr.op = UDMA_CMD_DESTROY_RC_CONTEXT;
	ret = udma_post_mbox(dev, mailbox, &mbox_attr);
	if (ret)
		dev_err(dev->dev, "failed to destroy rc queue ctx, ret = %d.\n", ret);

	udma_free_cmd_mailbox(dev, mailbox);

	return ret;
}

static int udma_alloc_rc_queue(struct udma_dev *dev,
			       struct ubcore_device_cfg *cfg, int rc_queue_id)
{
	uint32_t rcq_entry_size = dev->caps.rc_entry_size;
	uint32_t rcq_entry_num = cfg->rc_cfg.depth;
	struct udma_rc_queue *rcq;
	uint32_t size;
	int ret;

	rcq = kzalloc(sizeof(struct udma_rc_queue), GFP_KERNEL);
	if (!rcq)
		return -ENOMEM;
	rcq->id = rc_queue_id;

	size = rcq_entry_size * rcq_entry_num;
	rcq->buf.kva_or_slot = udma_alloc_iova(dev, size, &rcq->buf.addr);
	if (!rcq->buf.kva_or_slot) {
		ret = -ENOMEM;
		dev_err(dev->dev, "failed to alloc rc queue buffer.\n");
		goto err_alloc_rcq;
	}
	rcq->buf.entry_size = rcq_entry_size;
	rcq->buf.entry_cnt = rcq_entry_num;

	ret = udma_create_rc_queue_ctx(dev, rcq);
	if (ret) {
		dev_err(dev->dev,
			"failed to create rc queue ctx, rcq id %u, ret = %d.\n",
			rcq->id, ret);
		goto err_create_rcq_ctx;
	}

	ret = xa_err(xa_store(&dev->rc_table.xa, rcq->id, rcq, GFP_KERNEL));
	if (ret) {
		dev_err(dev->dev,
			"failed to stored rcq id to rc table, rcq id %d.\n",
			rc_queue_id);
		goto err_store_rcq_id;
	}

	return ret;

err_store_rcq_id:
	if (udma_destroy_rc_queue_ctx(dev, rcq))
		dev_err(dev->dev,
			"udma destroy rc queue ctx failed when alloc rc queue.\n");
err_create_rcq_ctx:
	udma_free_iova(dev, size, rcq->buf.kva_or_slot, rcq->buf.addr);
	rcq->buf.kva_or_slot = NULL;
	rcq->buf.addr = 0;
err_alloc_rcq:
	kfree(rcq);

	return ret;
}

void udma_free_rc_queue(struct udma_dev *dev, int rc_queue_id)
{
	struct udma_rc_queue *rcq;
	int ret;

	rcq = (struct udma_rc_queue *)xa_load(&dev->rc_table.xa, rc_queue_id);
	if (!rcq) {
		dev_warn(dev->dev,
			 "failed to find rcq, id = %d.\n", rc_queue_id);
		return;
	}

	xa_erase(&dev->rc_table.xa, rc_queue_id);
	ret = udma_destroy_rc_queue_ctx(dev, rcq);
	if (ret)
		dev_err(dev->dev,
			"udma destroy rc queue ctx failed, ret = %d.\n", ret);

	udma_free_iova(dev, rcq->buf.entry_size * rcq->buf.entry_cnt,
		       rcq->buf.kva_or_slot, rcq->buf.addr);
	rcq->buf.kva_or_slot = NULL;
	rcq->buf.addr = 0;
	kfree(rcq);
}

static int udma_config_rc_table(struct udma_dev *dev, struct ubcore_device_cfg *cfg)
{
	uint32_t rc_ctx_num = cfg->rc_cfg.rc_cnt;
	int ret = 0;
	int i;

	for (i = 0; i < rc_ctx_num; i++) {
		ret = udma_alloc_rc_queue(dev, cfg, i);
		if (ret) {
			dev_err(dev->dev, "failed to alloc rc queue.\n");
			goto err_alloc_rc_queue;
		}
	}
	dev->rc_table.ida_table.min = 0;
	dev->rc_table.ida_table.max = rc_ctx_num;

	return ret;

err_alloc_rc_queue:
	for (i -= 1; i >= 0; i--)
		udma_free_rc_queue(dev, i);

	return ret;
}

static int check_and_config_rc_table(struct udma_dev *dev, struct ubcore_device_cfg *cfg)
{
	int ret = 0;

	if (!cfg->mask.bs.rc_cnt && !cfg->mask.bs.rc_depth)
		return 0;

	if (!cfg->mask.bs.rc_cnt || !cfg->mask.bs.rc_depth) {
		dev_err(dev->dev, "Invalid rc mask, mask = %u.\n", cfg->mask.value);
		return -EINVAL;
	}

	if (!cfg->rc_cfg.rc_cnt || !cfg->rc_cfg.depth ||
	    cfg->rc_cfg.rc_cnt > dev->caps.rc_queue_num ||
	    cfg->rc_cfg.rc_cnt <= dev->caps.ack_queue_num) {
		dev_err(dev->dev,
			"Invalid rc param, rc cnt = %u, rc depth = %u, rc num = %u, ack queue num = %u.\n",
			cfg->rc_cfg.rc_cnt, cfg->rc_cfg.depth,
			dev->caps.rc_queue_num, dev->caps.ack_queue_num);
		return -EINVAL;
	}

	if (!test_and_set_bit_lock(RCT_INIT_FLAG, &dev->caps.init_flag))
		ret = udma_config_rc_table(dev, cfg);

	return ret;
}

int udma_config_device(struct ubcore_device *ubcore_dev,
		       struct ubcore_device_cfg *cfg)
{
	struct udma_dev *dev = to_udma_dev(ubcore_dev);
	int ret;

	if ((cfg->mask.bs.reserved_jetty_id_min && cfg->reserved_jetty_id_min != 0) ||
	    (cfg->mask.bs.reserved_jetty_id_max && cfg->reserved_jetty_id_max !=
	    dev->caps.public_jetty.max_cnt - 1)) {
		dev_err(dev->dev, "public jetty range must 0-%u.\n",
			dev->caps.public_jetty.max_cnt - 1);
		return -EINVAL;
	}

	ret = check_and_config_rc_table(dev, cfg);
	if (ret)
		dev_err(dev->dev, "failed to check device cfg, ret = %d.\n", ret);

	return ret;
}

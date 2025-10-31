// SPDX-License-Identifier: GPL-2.0+
/* Copyright(c) 2025 HiSilicon Technologies CO., Ltd. All rights reserved. */

#define dev_fmt(fmt) "UDMA: " fmt

#include <linux/slab.h>
#include <linux/dmapool.h>
#include <ub/ubase/ubase_comm_dev.h>
#include "udma_cmd.h"

bool debug_switch = true;

int udma_cmd_init(struct udma_dev *udma_dev)
{
	sema_init(&udma_dev->mb_cmd.poll_sem, 1);
	udma_dev->mb_cmd.pool = dma_pool_create("udma_cmd", udma_dev->dev,
						UDMA_MAILBOX_SIZE,
						UDMA_MAILBOX_SIZE, 0);
	if (!udma_dev->mb_cmd.pool) {
		dev_err(udma_dev->dev, "failed to dma_pool_create.\n");
		return -ENOMEM;
	}

	init_rwsem(&udma_dev->mb_cmd.udma_mb_rwsem);

	return 0;
}

void udma_cmd_cleanup(struct udma_dev *udma_dev)
{
	down_write(&udma_dev->mb_cmd.udma_mb_rwsem);
	dma_pool_destroy(udma_dev->mb_cmd.pool);
	up_write(&udma_dev->mb_cmd.udma_mb_rwsem);
}

struct ubase_cmd_mailbox *udma_alloc_cmd_mailbox(struct udma_dev *dev)
{
	struct ubase_cmd_mailbox *mailbox;

	mailbox = kzalloc(sizeof(*mailbox), GFP_KERNEL);
	if (!mailbox)
		goto failed_alloc_mailbox;

	down_read(&dev->mb_cmd.udma_mb_rwsem);
	mailbox->buf = dma_pool_zalloc(dev->mb_cmd.pool, GFP_KERNEL,
				       &mailbox->dma);
	if (!mailbox->buf) {
		dev_err(dev->dev, "failed to alloc buffer of mailbox.\n");
		goto failed_alloc_mailbox_buf;
	}

	return mailbox;

failed_alloc_mailbox_buf:
	up_read(&dev->mb_cmd.udma_mb_rwsem);
	kfree(mailbox);
failed_alloc_mailbox:
	return NULL;
}

void udma_free_cmd_mailbox(struct udma_dev *dev,
			   struct ubase_cmd_mailbox *mailbox)
{
	if (!mailbox) {
		dev_err(dev->dev, "Invalid mailbox.\n");
		return;
	}

	dma_pool_free(dev->mb_cmd.pool, mailbox->buf, mailbox->dma);
	up_read(&dev->mb_cmd.udma_mb_rwsem);
	kfree(mailbox);
}

static bool udma_op_ignore_eagain(uint8_t op, void *buf)
{
	struct udma_mbx_op_match matches[] = {
		{ UDMA_CMD_CREATE_JFS_CONTEXT, false },
		{ UDMA_CMD_MODIFY_JFS_CONTEXT, true },
		{ UDMA_CMD_DESTROY_JFS_CONTEXT, true },
		{ UDMA_CMD_QUERY_JFS_CONTEXT, true },
		{ UDMA_CMD_CREATE_JFC_CONTEXT, false },
		{ UDMA_CMD_MODIFY_JFC_CONTEXT, true },
		{ UDMA_CMD_DESTROY_JFC_CONTEXT, true },
		{ UDMA_CMD_QUERY_JFC_CONTEXT, true },
		{ UDMA_CMD_CREATE_JFR_CONTEXT, false },
		{ UDMA_CMD_MODIFY_JFR_CONTEXT, true },
		{ UDMA_CMD_DESTROY_JFR_CONTEXT, true },
		{ UDMA_CMD_QUERY_JFR_CONTEXT, true },
		{ UDMA_CMD_QUERY_TP_CONTEXT, true },
		{ UDMA_CMD_CREATE_JETTY_GROUP_CONTEXT, false },
		{ UDMA_CMD_MODIFY_JETTY_GROUP_CONTEXT, true },
		{ UDMA_CMD_DESTROY_JETTY_GROUP_CONTEXT, true },
		{ UDMA_CMD_QUERY_JETTY_GROUP_CONTEXT, true },
		{ UDMA_CMD_CREATE_RC_CONTEXT, false },
		{ UDMA_CMD_MODIFY_RC_CONTEXT, true },
		{ UDMA_CMD_DESTROY_RC_CONTEXT, true },
		{ UDMA_CMD_QUERY_RC_CONTEXT, true },
		{ UDMA_CMD_READ_SEID_UPI, true },
	};
	uint32_t i;

	for (i = 0; i < ARRAY_SIZE(matches); i++) {
		if (op == matches[i].op)
			return matches[i].ignore_ret;
	}

	return false;
}

int udma_post_mbox(struct udma_dev *dev, struct ubase_cmd_mailbox *mailbox,
		   struct ubase_mbx_attr *attr)
{
	int ret;

	if (debug_switch)
		dev_info_ratelimited(dev->dev,
				     "Send cmd mailbox, data: %08x %04x%04x.\n",
				     attr->tag, attr->op, attr->mbx_ue_id);

	ret = ubase_hw_upgrade_ctx_ex(dev->comdev.adev, attr, mailbox);

	return (ret == -EAGAIN &&
		udma_op_ignore_eagain(attr->op, mailbox->buf)) ? 0 : ret;
}

int udma_config_ctx_buf_to_hw(struct udma_dev *udma_dev,
			      struct udma_buf *ctx_buf,
			      struct ubase_mbx_attr *attr)
{
	struct ubase_cmd_mailbox mailbox;
	int ret;

	mailbox.dma = ctx_buf->addr;
	ret = udma_post_mbox(udma_dev, &mailbox, attr);
	if (ret)
		dev_err(udma_dev->dev,
			"failed to config ctx_buf to hw, ret = %d.\n", ret);

	return ret;
}

int udma_cmd_query_hw_resource(struct udma_dev *udma_dev, void *out_addr)
{
	struct ubase_cmd_buf out = {};
	struct ubase_cmd_buf in = {};

	udma_fill_buf(&in, UDMA_CMD_QUERY_UE_RES, true, 0, NULL);
	udma_fill_buf(&out, UDMA_CMD_QUERY_UE_RES, true,
		      sizeof(struct udma_cmd_ue_resource), out_addr);

	return ubase_cmd_send_inout(udma_dev->comdev.adev, &in, &out);
}

int post_mailbox_update_ctx(struct udma_dev *udma_dev, void *ctx, uint32_t size,
			    struct ubase_mbx_attr *attr)
{
	struct ubase_cmd_mailbox *mailbox;
	int ret;

	mailbox = udma_alloc_cmd_mailbox(udma_dev);
	if (!mailbox) {
		dev_err(udma_dev->dev,
			"failed to alloc mailbox for opcode 0x%x.\n", attr->op);
		return -ENOMEM;
	}

	if (ctx)
		memcpy(mailbox->buf, ctx, size);

	ret = udma_post_mbox(udma_dev, mailbox, attr);
	if (ret)
		dev_err(udma_dev->dev,
			"failed to post mailbox, opcode = 0x%x, ret = %d.\n", attr->op,
			ret);

	udma_free_cmd_mailbox(udma_dev, mailbox);

	return ret;
}

struct ubase_cmd_mailbox *udma_mailbox_query_ctx(struct udma_dev *udma_dev,
						struct ubase_mbx_attr *attr)
{
	struct ubase_cmd_mailbox *mailbox;
	int ret;

	mailbox = udma_alloc_cmd_mailbox(udma_dev);
	if (!mailbox) {
		dev_err(udma_dev->dev,
			"failed to alloc mailbox query ctx, opcode = %u, id = %u.\n",
			attr->op, attr->tag);
		return NULL;
	}

	ret = udma_post_mbox(udma_dev, mailbox, attr);
	if (ret) {
		dev_err(udma_dev->dev,
			"failed to post mailbox query ctx, opcode = %u, id = %u, ret = %d.\n",
			attr->op, attr->tag, ret);
		udma_free_cmd_mailbox(udma_dev, mailbox);
		return NULL;
	}

	return mailbox;
}

module_param(debug_switch, bool, 0444);
MODULE_PARM_DESC(debug_switch, "set debug print ON, default: true");

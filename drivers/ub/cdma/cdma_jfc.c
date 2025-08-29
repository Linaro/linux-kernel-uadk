// SPDX-License-Identifier: GPL-2.0+
/* Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved. */

#define dev_fmt(fmt) "CDMA: " fmt

#include <linux/delay.h>
#include "cdma_cmd.h"
#include "cdma_mbox.h"
#include "cdma_common.h"
#include "cdma_db.h"
#include "cdma_jfc.h"

static void cdma_jfc_id_free(struct cdma_dev *cdev, u32 jfcn)
{
	struct cdma_table *jfc_tbl = &cdev->jfc_table;
	unsigned long flags;

	spin_lock_irqsave(&jfc_tbl->lock, flags);
	idr_remove(&jfc_tbl->idr_tbl.idr, jfcn);
	spin_unlock_irqrestore(&jfc_tbl->lock, flags);
}

static struct cdma_jfc *cdma_id_find_jfc(struct cdma_dev *cdev, u32 jfcn)
{
	struct cdma_table *jfc_tbl = &cdev->jfc_table;
	struct cdma_jfc *jfc;
	unsigned long flags;

	spin_lock_irqsave(&jfc_tbl->lock, flags);
	jfc = idr_find(&jfc_tbl->idr_tbl.idr, jfcn);
	if (!jfc)
		dev_err(cdev->dev, "find jfc failed, id = %u.\n", jfcn);
	spin_unlock_irqrestore(&jfc_tbl->lock, flags);

	return jfc;
}

static void cdma_free_jfc_buf(struct cdma_dev *cdev, struct cdma_jfc *jfc)
{
	u32 size;

	if (!jfc->buf.kva) {
		cdma_unpin_sw_db(jfc->base.ctx, &jfc->db);
		cdma_unpin_queue_addr(jfc->buf.umem);
	} else {
		size = jfc->buf.entry_size * jfc->buf.entry_cnt;
		cdma_k_free_buf(cdev, size, &jfc->buf);
		cdma_free_sw_db(cdev, &jfc->db);
	}
}

static int cdma_query_jfc_destroy_done(struct cdma_dev *cdev, uint32_t jfcn)
{
	struct ubase_mbx_attr attr = { 0 };
	struct ubase_cmd_mailbox *mailbox;
	struct cdma_jfc_ctx *jfc_ctx;
	int ret;

	cdma_fill_mbx_attr(&attr, jfcn, CDMA_CMD_QUERY_JFC_CONTEXT, 0);
	mailbox = cdma_mailbox_query_ctx(cdev, &attr);
	if (!mailbox)
		return -ENOMEM;

	jfc_ctx = mailbox->buf;
	ret = jfc_ctx->pi == jfc_ctx->wr_cqe_idx ? 0 : -EAGAIN;

	cdma_free_cmd_mailbox(cdev, mailbox);

	return ret;
}

static int cdma_destroy_and_flush_jfc(struct cdma_dev *cdev, u32 jfcn)
{
#define QUERY_MAX_TIMES 5
	u32 wait_times = 0;
	int ret;

	ret = cdma_post_destroy_jfc_mbox(cdev, jfcn, CDMA_JFC_STATE_INVALID);
	if (ret) {
		dev_err(cdev->dev, "post mbox to destroy jfc failed, id: %u.\n", jfcn);
		return ret;
	}

	while (true) {
		if (!cdma_query_jfc_destroy_done(cdev, jfcn))
			return 0;
		if (wait_times > QUERY_MAX_TIMES)
			break;
		msleep(1 << wait_times);
		wait_times++;
	}
	dev_err(cdev->dev, "jfc flush time out, id = %u.\n", jfcn);

	return -ETIMEDOUT;
}

int cdma_post_destroy_jfc_mbox(struct cdma_dev *cdev, u32 jfcn,
			       enum cdma_jfc_state state)
{
	struct ubase_mbx_attr attr = { 0 };
	struct cdma_jfc_ctx ctx = { 0 };

	ctx.state = state;
	cdma_fill_mbx_attr(&attr, jfcn, CDMA_CMD_DESTROY_JFC_CONTEXT, 0);

	return cdma_post_mailbox_ctx(cdev, (void *)&ctx, sizeof(ctx), &attr);
}

int cdma_delete_jfc(struct cdma_dev *cdev, u32 jfcn,
		    struct cdma_cmd_delete_jfc_args *arg)
{
	struct cdma_jfc *jfc;
	int ret;

	if (!cdev)
		return -EINVAL;

	if (jfcn >= cdev->caps.jfc.max_cnt + cdev->caps.jfc.start_idx ||
		jfcn < cdev->caps.jfc.start_idx) {
		dev_err(cdev->dev,
			"jfc id invalid, jfcn = %u, start_idx = %u, max_cnt = %u.\n",
			jfcn, cdev->caps.jfc.start_idx,
			cdev->caps.jfc.max_cnt);
		return -EINVAL;
	}

	jfc = cdma_id_find_jfc(cdev, jfcn);
	if (!jfc) {
		dev_err(cdev->dev, "find jfc failed, jfcn = %u.\n", jfcn);
		return -EINVAL;
	}

	ret = cdma_destroy_and_flush_jfc(cdev, jfc->jfcn);
	if (ret)
		dev_err(cdev->dev, "jfc delete failed, jfcn = %u.\n", jfcn);

	cdma_free_jfc_buf(cdev, jfc);
	cdma_jfc_id_free(cdev, jfc->jfcn);

	pr_debug("Leave %s, jfcn: %u.\n", __func__, jfc->jfcn);

	kfree(jfc);

	return 0;
}

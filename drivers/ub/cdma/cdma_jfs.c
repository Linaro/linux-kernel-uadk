// SPDX-License-Identifier: GPL-2.0+
/* Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved. */

#define pr_fmt(fmt) "CDMA: " fmt
#define dev_fmt pr_fmt

#include <linux/io.h>
#include <linux/slab.h>
#include <linux/random.h>
#include <linux/delay.h>
#include <uapi/ub/cdma/cdma_abi.h>
#include "cdma_cmd.h"
#include "cdma_common.h"
#include "cdma_mbox.h"
#include "cdma_context.h"
#include "cdma_jfs.h"

static void cdma_free_sq_buf(struct cdma_dev *cdev, struct cdma_jetty_queue *sq)
{
	u32 size;

	if (sq->buf.kva) {
		size = sq->buf.entry_cnt * sq->buf.entry_size;
		cdma_k_free_buf(cdev, size, &sq->buf);
	} else {
		cdma_unpin_queue_addr(sq->buf.umem);
		sq->buf.umem = NULL;
	}
}

static inline void cdma_free_jfs_id(struct cdma_dev *cdev, u32 id)
{
	spin_lock(&cdev->jfs_table.lock);
	idr_remove(&cdev->jfs_table.idr_tbl.idr, id);
	spin_unlock(&cdev->jfs_table.lock);
}

static int cdma_set_jfs_state(struct cdma_dev *cdev, u32 jfs_id,
			      enum cdma_jetty_state state)
{
	struct cdma_jfs_ctx ctx[SZ_2] = { 0 };
	struct ubase_mbx_attr attr = { 0 };
	struct cdma_jfs_ctx *ctx_mask;

	ctx_mask = (struct cdma_jfs_ctx *)((char *)ctx + SZ_128);
	memset(ctx_mask, 0xff, sizeof(*ctx_mask));
	ctx->state = state;
	ctx_mask->state = 0;

	cdma_fill_mbx_attr(&attr, jfs_id, CDMA_CMD_MODIFY_JFS_CONTEXT, 0);

	return cdma_post_mailbox_ctx(cdev, (void *)ctx, sizeof(ctx), &attr);
}

static int cdma_query_jfs_ctx(struct cdma_dev *cdev,
			      struct cdma_jfs_ctx *jfs_ctx,
			      u32 jfs_id)
{
	struct ubase_mbx_attr attr = { 0 };
	struct ubase_cmd_mailbox *mailbox;

	cdma_fill_mbx_attr(&attr, jfs_id, CDMA_CMD_QUERY_JFS_CONTEXT, 0);
	mailbox = cdma_mailbox_query_ctx(cdev, &attr);
	if (!mailbox)
		return -ENOMEM;
	memcpy((void *)jfs_ctx, mailbox->buf, sizeof(*jfs_ctx));

	cdma_free_cmd_mailbox(cdev, mailbox);

	return 0;
}

static int cdma_destroy_hw_jfs_ctx(struct cdma_dev *cdev, u32 jfs_id)
{
	struct ubase_mbx_attr attr = { 0 };
	int ret;

	cdma_fill_mbx_attr(&attr, jfs_id, CDMA_CMD_DESTROY_JFS_CONTEXT, 0);
	ret = cdma_post_mailbox_ctx(cdev, NULL, 0, &attr);
	if (ret)
		dev_err(cdev->dev,
			"post mailbox destroy jfs ctx failed, ret = %d.\n", ret);

	return ret;
}

static bool cdma_wait_timeout(u32 *sum_times, u32 times, u32 ta_timeout)
{
	u32 wait_time;

	if (*sum_times > ta_timeout)
		return true;

	wait_time = 1 << times;
	msleep(wait_time);
	*sum_times += wait_time;

	return false;
}

static bool cdma_query_jfs_fd(struct cdma_dev *cdev,
			      struct cdma_jetty_queue *sq)
{
	struct cdma_jfs_ctx ctx = { 0 };
	u16 rcv_send_diff = 0;
	u32 sum_times = 0;
	u32 times = 0;

	while (true) {
		if (cdma_query_jfs_ctx(cdev, &ctx, sq->id))
			return false;

		if (ctx.flush_cqe_done)
			return true;

		if (cdma_wait_timeout(&sum_times, times, sq->ta_tmo)) {
			dev_warn(cdev->dev,
				 "ta timeout, id = %u. PI = %u, CI = %u, next_send_ssn = %u next_rcv_ssn = %u state = %u.\n",
				 sq->id, ctx.pi, ctx.ci, ctx.next_send_ssn,
				 ctx.next_rcv_ssn, ctx.state);
			break;
		}

		times++;
	}

	/* In the flip scenario, ctx.next_rcv_ssn - ctx.next_send_ssn value is less than 512. */
	rcv_send_diff = ctx.next_rcv_ssn - ctx.next_send_ssn;
	if (ctx.flush_ssn_vld && rcv_send_diff < CDMA_RCV_SEND_MAX_DIFF)
		return true;

	dev_err(cdev->dev, "query jfs flush ssn error, id = %u", sq->id);

	return false;
}

int cdma_modify_jfs_precondition(struct cdma_dev *cdev,
				 struct cdma_jetty_queue *sq)
{
	struct cdma_jfs_ctx ctx = { 0 };
	u16 rcv_send_diff = 0;
	u32 sum_times = 0;
	u32 times = 0;

	while (true) {
		if (cdma_query_jfs_ctx(cdev, &ctx, sq->id)) {
			dev_err(cdev->dev, "query jfs ctx failed, id = %u.\n",
				sq->id);
			return -ENOMEM;
		}

		rcv_send_diff = ctx.next_rcv_ssn - ctx.next_send_ssn;
		if ((ctx.pi == ctx.ci) && (rcv_send_diff < CDMA_RCV_SEND_MAX_DIFF) &&
		    (ctx.state == CDMA_JETTY_READY))
			break;

		if ((rcv_send_diff < CDMA_RCV_SEND_MAX_DIFF) &&
		    (ctx.state == CDMA_JETTY_ERROR))
			break;

		if (cdma_wait_timeout(&sum_times, times, sq->ta_tmo)) {
			dev_warn(cdev->dev,
				 "ta timeout, id = %u. PI = %u, CI = %u, next_send_ssn = %u next_rcv_ssn = %u state = %u.\n",
				 sq->id, ctx.pi, ctx.ci, ctx.next_send_ssn,
				 ctx.next_rcv_ssn, ctx.state);
			break;
		}
		times++;
	}

	return 0;
}

static bool cdma_destroy_jfs_precondition(struct cdma_dev *cdev,
					  struct cdma_jetty_queue *sq)
{
#define CDMA_DESTROY_JETTY_DELAY_TIME 100U

	if ((sq->state == CDMA_JETTY_READY) ||
	    (sq->state == CDMA_JETTY_SUSPENDED)) {
		if (cdma_modify_jfs_precondition(cdev, sq))
			return false;

		if (cdma_set_jfs_state(cdev, sq->id, CDMA_JETTY_ERROR)) {
			dev_err(cdev->dev, "modify jfs state to error failed, id = %u.\n",
				sq->id);
			return false;
		}

		sq->state = CDMA_JETTY_ERROR;
		dev_dbg(cdev->dev, "set jfs %u status finished.\n", sq->id);
	}

	if (!cdma_query_jfs_fd(cdev, sq))
		return false;

	udelay(CDMA_DESTROY_JETTY_DELAY_TIME);

	return true;
}

static int cdma_modify_and_destroy_jfs(struct cdma_dev *cdev,
				       struct cdma_jetty_queue *sq)
{
	int ret = 0;

	if (!cdma_destroy_jfs_precondition(cdev, sq))
		return -EINVAL;

	if (sq->state != CDMA_JETTY_RESET)
		ret = cdma_destroy_hw_jfs_ctx(cdev, sq->id);

	return ret;
}

int cdma_delete_jfs(struct cdma_dev *cdev, u32 jfs_id)
{
	struct cdma_jfs *jfs;
	int ret;

	if (jfs_id >= cdev->caps.jfs.start_idx + cdev->caps.jfs.max_cnt) {
		dev_info(cdev->dev,
			 "jfs id invalid, jfs_id = %u, start_idx = %u, max_cnt = %u.\n",
			 jfs_id, cdev->caps.jfs.start_idx,
			 cdev->caps.jfs.max_cnt);
		return -EINVAL;
	}

	spin_lock(&cdev->jfs_table.lock);
	jfs = idr_find(&cdev->jfs_table.idr_tbl.idr, jfs_id);
	spin_unlock(&cdev->jfs_table.lock);
	if (!jfs) {
		dev_err(cdev->dev, "get jfs from table failed, id = %u.\n", jfs_id);
		return -EINVAL;
	}

	ret = cdma_modify_and_destroy_jfs(cdev, &jfs->sq);
	if (ret)
		dev_err(cdev->dev, "jfs delete failed, id = %u.\n", jfs->id);

	cdma_free_sq_buf(cdev, &jfs->sq);

	cdma_free_jfs_id(cdev, jfs_id);

	pr_debug("Leave %s, jfsn: %u.\n", __func__, jfs_id);

	kfree(jfs);

	return 0;
}

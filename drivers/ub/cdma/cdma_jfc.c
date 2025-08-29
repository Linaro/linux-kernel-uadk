// SPDX-License-Identifier: GPL-2.0+
/* Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved. */

#define dev_fmt(fmt) "CDMA: " fmt

#include <linux/delay.h>
#include "cdma_cmd.h"
#include "cdma_context.h"
#include "cdma_mbox.h"
#include "cdma_common.h"
#include "cdma_db.h"
#include "cdma_jfc.h"

static int cdma_get_cmd_from_user(struct cdma_create_jfc_ucmd *ucmd,
				  struct cdma_dev *cdev,
				  struct cdma_udata *udata,
				  struct cdma_jfc *jfc,
				  struct cdma_jfc_cfg *cfg)
{
	struct cdma_context *ctx;
	u32 depth = cfg->depth;
	int ret;

	if (!udata) {
		jfc->arm_sn = 1;
		jfc->buf.entry_cnt = depth ? roundup_pow_of_two(depth) : depth;
		return 0;
	}

	if (!udata->udrv_data || !udata->udrv_data->in_addr ||
		udata->udrv_data->in_len != (u32)sizeof(*ucmd)) {
		dev_err(cdev->dev, "invalid parameter.\n");
		return -EINVAL;
	}

	ret = (int)copy_from_user(ucmd, (void *)udata->udrv_data->in_addr,
				  (u32)sizeof(*ucmd));
	if (ret) {
		dev_err(cdev->dev,
			"copy udata from user failed, ret = %d.\n", ret);
		return -EFAULT;
	}

	jfc->mode = ucmd->mode;
	jfc->db.db_addr = ucmd->db_addr;

	ctx = udata->uctx;
	jfc->base.ctx = ctx;
	jfc->tid = ctx->tid;

	if (cdev->caps.cqe_size == CDMA_DEFAULT_CQE_SIZE)
		jfc->buf.entry_cnt = ucmd->buf_len >> CDMA_JFC_DEFAULT_CQE_SHIFT;
	else
		jfc->buf.entry_cnt = ucmd->buf_len >> CDMA_JFC_OTHER_CQE_SHIFT;

	return ret;
}

static int cdma_check_jfc_cfg(struct cdma_dev *cdev, struct cdma_jfc *jfc,
			      struct cdma_jfc_cfg *cfg)
{
	if (!jfc->buf.entry_cnt || jfc->buf.entry_cnt > cdev->caps.jfc.depth) {
		dev_err(cdev->dev, "invalid jfc depth = %u, cap depth = %u.\n",
			jfc->buf.entry_cnt, cdev->caps.jfc.depth);
		return -EINVAL;
	}

	if (jfc->buf.entry_cnt < CDMA_JFC_DEPTH_MIN)
		jfc->buf.entry_cnt = CDMA_JFC_DEPTH_MIN;

	if (cfg->ceqn >= cdev->caps.comp_vector_cnt) {
		dev_err(cdev->dev, "invalid ceqn = %u, cap ceq cnt = %u.\n",
			cfg->ceqn, cdev->caps.comp_vector_cnt);
		return -EINVAL;
	}

	return 0;
}

static void cdma_init_jfc_param(struct cdma_jfc_cfg *cfg, struct cdma_jfc *jfc)
{
	jfc->base.id = jfc->jfcn;
	jfc->base.jfc_cfg = *cfg;
	jfc->ceqn = cfg->ceqn;
}

static int cdma_jfc_id_alloc(struct cdma_dev *cdev, struct cdma_jfc *jfc)
{
	struct cdma_table *jfc_tbl = &cdev->jfc_table;
	u32 min = jfc_tbl->idr_tbl.min;
	u32 max = jfc_tbl->idr_tbl.max;
	unsigned long flags;
	int id;

	idr_preload(GFP_KERNEL);
	spin_lock_irqsave(&jfc_tbl->lock, flags);
	id = idr_alloc(&jfc_tbl->idr_tbl.idr, jfc, jfc_tbl->idr_tbl.next, max,
		       GFP_NOWAIT);
	if (id < 0) {
		id = idr_alloc(&jfc_tbl->idr_tbl.idr, jfc, min, max,
			       GFP_NOWAIT);
		if (id < 0)
			dev_err(cdev->dev, "alloc jfc id failed.\n");
	}

	jfc_tbl->idr_tbl.next = (id >= 0 && id + 1 <= max) ? id + 1 : min;
	spin_unlock_irqrestore(&jfc_tbl->lock, flags);
	idr_preload_end();

	jfc->jfcn = id;

	return id;
}

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

static int cdma_get_jfc_buf(struct cdma_dev *cdev,
			    struct cdma_create_jfc_ucmd *ucmd,
			    struct cdma_udata *udata, struct cdma_jfc *jfc)
{
	u32 size;
	int ret;

	if (udata) {
		jfc->buf.umem = cdma_umem_get(cdev, ucmd->buf_addr,
					      ucmd->buf_len, false);
		if (IS_ERR(jfc->buf.umem)) {
			ret = PTR_ERR(jfc->buf.umem);
			dev_err(cdev->dev, "get umem failed, ret = %d.\n",
				ret);
			return ret;
		}
		jfc->buf.addr = ucmd->buf_addr;
		ret = cdma_pin_sw_db(jfc->base.ctx, &jfc->db);
		if (ret)
			cdma_umem_release(jfc->buf.umem, false);

		return ret;
	}

	spin_lock_init(&jfc->lock);
	jfc->buf.entry_size = cdev->caps.cqe_size;
	jfc->tid = cdev->tid;
	size = jfc->buf.entry_size * jfc->buf.entry_cnt;
	ret = cdma_k_alloc_buf(cdev, size, &jfc->buf);
	if (ret) {
		dev_err(cdev->dev, "alloc buffer for jfc failed.\n");
		return ret;
	}

	ret = cdma_alloc_sw_db(cdev, &jfc->db);
	if (ret) {
		dev_err(cdev->dev, "alloc sw db for jfc failed: %u.\n",
			jfc->jfcn);
		cdma_k_free_buf(cdev, size, &jfc->buf);
	}

	return ret;
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

static void cdma_construct_jfc_ctx(struct cdma_dev *cdev,
				   struct cdma_jfc *jfc,
				   struct cdma_jfc_ctx *ctx)
{
	memset(ctx, 0, sizeof(*ctx));

	ctx->state = CDMA_JFC_STATE_VALID;
	ctx->arm_st = jfc_arm_mode ? CDMA_CTX_NO_ARMED : CDMA_CTX_ALWAYS_ARMED;
	ctx->shift = ilog2(jfc->buf.entry_cnt) - CDMA_JFC_DEPTH_SHIFT_BASE;

	if (cdev->caps.cqe_size == CDMA_DEFAULT_CQE_SIZE)
		ctx->cqe_size = CDMA_128_CQE_SIZE;
	else
		ctx->cqe_size = CDMA_64_CQE_SIZE;

	ctx->record_db_en = CDMA_RECORD_EN;
	ctx->jfc_type = CDMA_NORMAL_JFC_TYPE;
	ctx->cqe_va_l = jfc->buf.addr >> CQE_VA_L_OFFSET;
	ctx->cqe_va_h = jfc->buf.addr >> CQE_VA_H_OFFSET;
	ctx->cqe_token_id = jfc->tid;

	if (cqe_mode)
		ctx->cq_cnt_mode = CDMA_CQE_CNT_MODE_BY_CI_PI_GAP;
	else
		ctx->cq_cnt_mode = CDMA_CQE_CNT_MODE_BY_COUNT;

	ctx->ceqn = jfc->ceqn;
	ctx->record_db_addr_l = jfc->db.db_addr >> CDMA_DB_L_OFFSET;
	ctx->record_db_addr_h = jfc->db.db_addr >> CDMA_DB_H_OFFSET;
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

static int cdma_post_create_jfc_mbox(struct cdma_dev *cdev, struct cdma_jfc *jfc)
{
	struct ubase_mbx_attr attr = { 0 };
	struct cdma_jfc_ctx ctx = { 0 };

	cdma_construct_jfc_ctx(cdev, jfc, &ctx);
	cdma_fill_mbx_attr(&attr, jfc->jfcn, CDMA_CMD_CREATE_JFC_CONTEXT, 0);

	return cdma_post_mailbox_ctx(cdev, (void *)&ctx, sizeof(ctx), &attr);
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

struct cdma_base_jfc *cdma_create_jfc(struct cdma_dev *cdev,
				      struct cdma_jfc_cfg *cfg,
				      struct cdma_udata *udata)
{
	struct cdma_create_jfc_ucmd ucmd = { 0 };
	struct cdma_jfc *jfc;
	int ret;

	jfc = kzalloc(sizeof(*jfc), GFP_KERNEL);
	if (!jfc)
		return NULL;

	ret = cdma_get_cmd_from_user(&ucmd, cdev, udata, jfc, cfg);
	if (ret)
		goto err_get_cmd;

	ret = cdma_check_jfc_cfg(cdev, jfc, cfg);
	if (ret)
		goto err_check_cfg;

	ret = cdma_jfc_id_alloc(cdev, jfc);
	if (ret < 0)
		goto err_alloc_jfc_id;

	cdma_init_jfc_param(cfg, jfc);
	ret = cdma_get_jfc_buf(cdev, &ucmd, udata, jfc);
	if (ret)
		goto err_get_jfc_buf;

	ret = cdma_post_create_jfc_mbox(cdev, jfc);
	if (ret)
		goto err_alloc_cqc;

	jfc->base.dev = cdev;

	dev_dbg(cdev->dev, "create jfc id = %u, queue id = %u.\n",
		jfc->jfcn, cfg->queue_id);

	return &jfc->base;

err_alloc_cqc:
	cdma_free_jfc_buf(cdev, jfc);
err_get_jfc_buf:
	cdma_jfc_id_free(cdev, jfc->jfcn);
err_alloc_jfc_id:
err_check_cfg:
err_get_cmd:
	kfree(jfc);
	return NULL;
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

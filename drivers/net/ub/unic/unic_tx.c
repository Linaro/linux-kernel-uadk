// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved.
 *
 */

#define dev_fmt(fmt) "unic: (pid %d) " fmt, current->pid

#include <linux/if_vlan.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/limits.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <linux/jiffies.h>
#include <net/ipv6.h>
#ifdef CONFIG_UB_UNIC_UBL
#include <net/ub/ubl.h>
#endif
#include <ub/ubase/ubase_comm_mbx.h>

#include "unic_dev.h"
#include "unic_hw.h"
#include "unic_tx.h"

/* When use tracepoint, must define "CREATE_TRACE_POINTS" before include the
 * trace header file.
 * If many source file need include the same header file, the
 * "CREATE_TRACE_POINTS" only define once.
 */
#define CREATE_TRACE_POINTS
#include "unic_trace.h"

#define UNIC_CSUM_OFFLOAD_DISABLE	0
#define UNIC_CSUM_OFFLOAD_ENABLE	1
#define IP_VERSION_IPV4			0x4
#define IP_VERSION_IPV6			0x6

#define UNIC_CC_DEFAULT_FECN_MODE	0x4000
#define UNIC_SGE_MAX_PAYLOAD		UINT_MAX
#define UNIC_MIN_TX_LEN			60U
#define UNIC_SQE_CTRL_SECTION_NUM	2
#define UNIC_SQE_MAX_SGE_NUM		18
#define UNIC_SQEBB_MAX_SGE_NUM		4
#define UNIC_HEADER_LEN_2B_OFFSET	1
#define UNIC_HEADER_LEN_4B_OFFSET	2
#define UNIC_SKB_FRAGS_START_INDEX	1
#define UNIC_SQEBB_POINT_REVERSE	(USHRT_MAX + 1)
#define UNIC_RCV_SEND_MAX_DIFF_VAL	512U

#define unic_sqebb_cnt(sge_num) DIV_ROUND_UP((sge_num), 4)

static inline u16 unic_get_sqe_depth(struct unic_sq *sq)
{
	struct unic_dev *unic_dev = netdev_priv(sq->netdev);

	return unic_dev->channels.sqebb_depth;
}

static inline u16 unic_get_sqe_mask(struct unic_sq *sq)
{
	return unic_get_sqe_depth(sq) - 1;
}

static u16 unic_get_spare_sqebb(struct unic_sq *sq)
{
	u16 sqe_depth = unic_get_sqe_depth(sq);
	u32 pi = sq->pi;

	if (unlikely(sq->pi < sq->ci))
		pi += UNIC_SQEBB_POINT_REVERSE;

	return sqe_depth - (pi - sq->ci);
}

static void unic_unmap_skb_buffer(struct unic_sq *sq, u32 info_index,
				  u8 skb_map_num, u16 sqebb_mask)
{
	struct unic_dev *unic_dev = netdev_priv(sq->netdev);
	struct unic_sqe_ctrl_section *ctrl;
	struct unic_sqe_sge_section *sge;
	struct device *dev;
	u8 i;

	ctrl = (struct unic_sqe_ctrl_section *)&sq->sqebb[info_index & sqebb_mask];
	sge = (struct unic_sqe_sge_section *)(ctrl + 1);
	dev = &unic_dev->comdev.adev->dev;
	dma_unmap_single(dev->parent, sge->address, sge->length, DMA_TO_DEVICE);

	for (i = UNIC_SKB_FRAGS_START_INDEX; i < skb_map_num; i++) {
		if (!((i + UNIC_SQE_CTRL_SECTION_NUM) % UNIC_SQEBB_MAX_SGE_NUM)) {
			info_index++;
			sge = (struct unic_sqe_sge_section *)
			      &sq->sqebb[info_index & sqebb_mask];
		} else {
			sge++;
		}

		dma_unmap_page(dev->parent, sge->address, sge->length, DMA_TO_DEVICE);
	}
}

static void unic_consume_skb(struct sk_buff *skb, u32 info_index,
			     struct unic_sq *sq, int budget, u16 sqebb_mask)
{
	struct unic_sqe_ctrl_section *ctrl;

	ctrl = (struct unic_sqe_ctrl_section *)&sq->sqebb[info_index & sqebb_mask];
	unic_unmap_skb_buffer(sq, info_index, ctrl->sge_num, sqebb_mask);

	napi_consume_skb(skb, budget);
}

static bool unic_check_hw_ci_valid(u16 hw_ci, u16 sq_ci, struct unic_sq *sq)
{
	u16 sqebb_mask = unic_get_sqe_mask(sq);
	struct unic_sqe_ctrl_section *ctrl;
	u16 sqebb_cnt, sw_ci;

	ctrl = (struct unic_sqe_ctrl_section *)&sq->sqebb[sq_ci & sqebb_mask];
	sqebb_cnt = unic_sqebb_cnt(ctrl->sge_num + UNIC_SQE_CTRL_SECTION_NUM);
	sw_ci = sq_ci + sqebb_cnt - 1;
	if (unlikely(sw_ci != hw_ci)) {
		unic_sq_stats_inc(sq, ci_mismatch);
		return false;
	}

	return true;
}

static void unic_reclaim_single_sqe_space(struct unic_sq *sq, u16 sqebb_mask,
					  u16 *sq_ci)
{
	struct unic_sqe_ctrl_section *ctrl;
	u16 ci = *sq_ci & sqebb_mask;
	u8 sge_num, sqebb_cnt;

	ctrl = (struct unic_sqe_ctrl_section *)&sq->sqebb[ci];
	sge_num = ctrl->sge_num;
	sqebb_cnt = unic_sqebb_cnt(sge_num + UNIC_SQE_CTRL_SECTION_NUM);
	if (unlikely(ci + sqebb_cnt - 1 > sqebb_mask)) {
		memset(ctrl, 0, (sqebb_mask - ci + 1) * sizeof(struct unic_sqebb));
		memset(&sq->sqebb[0], 0,
		       (ci + sqebb_cnt - 1 - sqebb_mask) * sizeof(struct unic_sqebb));
	} else {
		memset(ctrl, 0, sqebb_cnt * sizeof(struct unic_sqebb));
	}

	*sq_ci += sqebb_cnt;
}

static void unic_flush_unused_sqe(struct unic_sq *sq, u16 sqebb_mask,
				  u16 *sq_ci)
{
	struct sk_buff *skb;

	while (*sq_ci != sq->pi) {
		skb = sq->skbs[*sq_ci & sqebb_mask];
		unic_consume_skb(skb, *sq_ci, sq, 0, sqebb_mask);
		unic_reclaim_single_sqe_space(sq, sqebb_mask, sq_ci);
	}
}

static bool unic_check_hw_ci_late(struct unic_sq *sq, u16 sq_ci)
{
	u32 effect_num, actual_num;

	if (likely(!sq->check_ci_late))
		return false;

	effect_num = sq->pi < sq->start_pi ?
		     (u32)sq->pi + UNIC_SQEBB_POINT_REVERSE - (u32)sq->start_pi :
		     (u32)sq->pi - (u32)sq->start_pi;

	actual_num = sq->pi < sq_ci ?
		    (u32)sq->pi + UNIC_SQEBB_POINT_REVERSE - (u32)sq_ci :
		    (u32)sq->pi - (u32)sq_ci;

	if (unlikely(actual_num > effect_num)) {
		unic_sq_stats_inc(sq, drop_cnt);
		return true;
	}

	sq->check_ci_late = false;
	return false;
}

static bool unic_reclaim_sq_space(struct unic_sq *sq, int budget, u64 *bytes,
				  u64 *packets, bool clear)
{
	struct net_device *netdev = sq->netdev;
	struct unic_dev *unic_dev = netdev_priv(netdev);
	u8 jfc_shift = unic_dev->channels.sq_jfc_shift;
	u16 sqebb_mask = unic_get_sqe_mask(sq);
	union unic_cqe *cqe = sq->cq->cqe;
	struct unic_cq *cq = sq->cq;
	u32 cq_mask, cq_ci = cq->ci;
	bool reclaimed = false;
	struct sk_buff *skb;
	u16 sq_ci = sq->ci;

	cq_mask = unic_get_sq_cqe_mask(unic_dev);
	cqe = &cq->cqe[cq->ci & cq_mask];
	while (unic_cqe_owner_is_soft(jfc_shift, cq->ci, cqe->tx.owner)) {
		trace_unic_tx_cqe(netdev, cq, sq->pi, sq_ci, cq_mask);
		if (unlikely(cqe->tx.fd)) {
			cq->ci++;
			unic_sq_stats_inc(sq, fd_cnt);
			unic_flush_unused_sqe(sq, sqebb_mask, &sq_ci);
			reclaimed = true;
			break;
		}

		if (unlikely(!unic_check_hw_ci_valid(cqe->tx.raw_ci, sq_ci, sq)))
			break;

		skb = sq->skbs[sq_ci & sqebb_mask];

		if (!clear && likely(!unic_check_hw_ci_late(sq, sq_ci))) {
			*bytes += skb->len;
			(*packets)++;
		}

		unic_consume_skb(skb, sq_ci, sq, budget, sqebb_mask);
		unic_reclaim_single_sqe_space(sq, sqebb_mask, &sq_ci);

		reclaimed = true;
		cq->ci++;
		cqe = &cq->cqe[cq->ci & cq_mask];
	}

	unic_cq_doorbell(cq, cq_ci);
	sq->ci = sq_ci;
	return reclaimed;
}

void unic_poll_tx(struct unic_sq *sq, int budget)
{
#define UNIC_MIN_SPARE_SQEBB DIV_ROUND_UP(UNIC_SQE_CTRL_SECTION_NUM + \
					  UNIC_SQE_MAX_SGE_NUM, \
					  UNIC_SQEBB_MAX_SGE_NUM)

	struct net_device *netdev = sq->netdev;
	struct netdev_queue *dev_queue;
	struct unic_dev *unic_dev;
	u64 packets = 0;
	u64 bytes = 0;

	if (unlikely(!unic_reclaim_sq_space(sq, budget, &bytes, &packets, false)))
		return;

	u64_stats_update_begin(&sq->syncp);
	sq->stats.bytes += bytes;
	sq->stats.packets += packets;
	u64_stats_update_end(&sq->syncp);

	dev_queue = netdev_get_tx_queue(netdev, sq->queue_index);
	netdev_tx_completed_queue(dev_queue, packets, bytes);

	if (unlikely(netif_carrier_ok(netdev) &&
		     unic_get_spare_sqebb(sq) >= UNIC_MIN_SPARE_SQEBB)) {
		unic_dev = netdev_priv(netdev);
		if (netif_tx_queue_stopped(dev_queue) &&
		    !test_bit(UNIC_STATE_DOWN, &unic_dev->state)) {
			netif_tx_wake_queue(dev_queue);
			unic_sq_stats_inc(sq, restart_queue);
		}
	}
}

static u8 unic_sq_get_sl(struct unic_dev *unic_dev, u32 idx)
{
	struct auxiliary_device *adev = unic_dev->comdev.adev;
	struct ubase_adev_qos *qos = ubase_get_adev_qos(adev);
	struct unic_vl *vl = &unic_dev->channels.vl;
	int i;

	for (i = 0; i < unic_dev->channels.rss_vl_num; i++)
		if (idx < vl->queue_offset[i] + vl->queue_count[i])
			break;

	return i >= unic_dev->channels.rss_vl_num ?
		    vl->vl_sl[qos->nic_vl[0]] :
		    vl->vl_sl[qos->nic_vl[i]];
}

static void unic_init_jfs_ctx(struct unic_dev *unic_dev, struct unic_sq *sq,
			      u32 idx, u32 tid)
{
	struct unic_jfs_ctx *ctx = &sq->jfs_ctx;

	ctx->ta_timeout = UNIC_TIMEOUT_8S;
	ctx->type = UNIC_RAW_TYPE;
	ctx->sqe_bb_shift = unic_dev->channels.sqebb_shift;
	ctx->state = UNIC_JFS_STATE_READY;
	ctx->sl = unic_sq_get_sl(unic_dev, idx) & UNIC_SQE_SL_BIT;
	ctx->jfs_mode = UNIC_JFS;
	ctx->sqe_token_id_l = tid & (u32)UNIC_SQE_TOKEN_ID_L_MASK;
	ctx->sqe_token_id_h = (tid >> UNIC_SQE_TOKEN_ID_H_OFFSET) &
			      (u32)UNIC_SQE_TOKEN_ID_H_MASK;
	ctx->sqe_base_addr_l = (sq->sqebb_dma_addr >> UNIC_SQE_VA0_OFFSET) &
				UNIC_SQE_VA0_VALID_BIT;
	ctx->sqe_base_addr_h = (sq->sqebb_dma_addr >> UNIC_SQE_VA1_OFFSET) &
				UNIC_SQE_VA1_VALID_BIT;
	ctx->tx_jfcn = idx;
	ctx->sqe_pld_tokenid = tid;
	ctx->avail_sgmt_ost = UNIC_AVAIL_SGMT_OST_INIT;
}

static int unic_mbx_create_jfs_context(struct auxiliary_device *adev,
				       struct unic_sq *sq, u32 idx)
{
	struct ubase_cmd_mailbox *mailbox;
	struct ubase_mbx_attr attr = {0};
	int ret;

	mailbox = ubase_alloc_cmd_mailbox(adev);
	if (IS_ERR_OR_NULL(mailbox)) {
		dev_err(adev->dev.parent,
			"failed to alloc mailbox for create jfs context, idx = %u.\n",
			idx);
		return -ENOMEM;
	}

	memcpy(mailbox->buf, &sq->jfs_ctx, sizeof(struct unic_jfs_ctx));
	ubase_fill_mbx_attr(&attr, idx, UBASE_MB_CREATE_JFS_CONTEXT, 0);
	ret = ubase_hw_upgrade_ctx_ex(adev, &attr, mailbox);
	if (ret)
		dev_err(adev->dev.parent,
			"failed to post create jfs ctx mbx, idx = %u, ret = %d.\n",
			idx, ret);

	ubase_free_cmd_mailbox(adev, mailbox);

	return ret;
}

static void unic_modify_jfs_state_to_error(struct auxiliary_device *adev,
					   u32 idx)
{
	struct unic_jfs_ctx *ctx, *ctx_mask;
	struct ubase_cmd_mailbox *mailbox;
	struct ubase_mbx_attr attr = {0};
	int ret;

	mailbox = ubase_alloc_cmd_mailbox(adev);
	if (IS_ERR_OR_NULL(mailbox)) {
		dev_err(adev->dev.parent,
			"failed to alloc mbx for set jfs state.\n");
		return;
	}

	ctx = (struct unic_jfs_ctx *)mailbox->buf;
	ctx_mask = ctx + 1;
	memset(ctx_mask, 0xff, sizeof(struct unic_jfs_ctx));
	ctx->state = UNIC_JFS_STATE_ERROR;
	ctx_mask->state = 0;

	ubase_fill_mbx_attr(&attr, idx, UBASE_MB_MODIFY_JFS_CONTEXT, 0);
	ret = ubase_hw_upgrade_ctx_ex(adev, &attr, mailbox);
	if (ret)
		dev_err(adev->dev.parent,
			"failed to set jfs state, ret=%d.\n", ret);

	ubase_free_cmd_mailbox(adev, mailbox);
}

static u16 unic_get_ta_timeout_ms(u8 ta_timeout)
{
#define UNIC_DEFINE_128MS	128
#define UNIC_DEFINE_1S		1000
#define UNIC_DEFINE_8S		8000
#define UNIC_DEFINE_64S		64000

	switch (ta_timeout) {
	case UNIC_TIMEOUT_128MS:
		return UNIC_DEFINE_128MS;
	case UNIC_TIMEOUT_1S:
		return UNIC_DEFINE_1S;
	case UNIC_TIMEOUT_8S:
		return UNIC_DEFINE_8S;
	default:
		return UNIC_DEFINE_64S;
	}
}

static bool unic_jfs_schedule_complete(struct unic_jfs_ctx *ctx)
{
	u16 rcv_send_diff;

	rcv_send_diff = le16_to_cpu(ctx->next_rcv_ssn) -
			le16_to_cpu(ctx->next_send_ssn);
	if (ctx->state == UNIC_JFS_STATE_READY && ctx->PI == ctx->CI &&
	    rcv_send_diff < UNIC_RCV_SEND_MAX_DIFF_VAL)
		return true;

	if (ctx->state == UNIC_JFS_STATE_ERROR &&
	    rcv_send_diff < UNIC_RCV_SEND_MAX_DIFF_VAL)
		return true;

	return false;
}

static int unic_jfs_flush_prepare(struct auxiliary_device *adev, u32 idx)
{
	struct ubase_cmd_mailbox *mailbox;
	struct ubase_mbx_attr attr = {0};
	struct unic_jfs_ctx *ctx;
	int ret;

	mailbox = ubase_alloc_cmd_mailbox(adev);
	if (IS_ERR_OR_NULL(mailbox))
		return -ENOMEM;

	ubase_fill_mbx_attr(&attr, idx, UBASE_MB_QUERY_JFS_CONTEXT, 0);

	ret = ubase_hw_upgrade_ctx_ex(adev, &attr, mailbox);
	if (ret)
		goto out;

	ctx = (struct unic_jfs_ctx *)mailbox->buf;
	if (!unic_jfs_schedule_complete(ctx))
		ret = -EBUSY;
out:
	ubase_free_cmd_mailbox(adev, mailbox);
	return ret;
}

static inline bool unic_jfs_flush_ssn_vld(struct unic_jfs_ctx *ctx)
{
	u16 rcv_send_diff = le16_to_cpu(ctx->next_rcv_ssn) -
			    le16_to_cpu(ctx->next_send_ssn);

	return ctx->flush_ssn_vld &&
	       (rcv_send_diff < UNIC_RCV_SEND_MAX_DIFF_VAL);
}

static inline bool unic_jfs_flush_cqe_done(struct unic_jfs_ctx *ctx)
{
	return ctx->flush_cqe_done;
}

static int unic_check_jfs_flush_done(struct auxiliary_device *adev, u32 idx,
				     bool (*check_fd)(struct unic_jfs_ctx *ctx))
{
	struct ubase_cmd_mailbox *mailbox;
	struct ubase_mbx_attr attr = {0};
	struct unic_jfs_ctx *ctx;
	int ret;

	mailbox = ubase_alloc_cmd_mailbox(adev);
	if (IS_ERR_OR_NULL(mailbox))
		return -ENOMEM;

	ubase_fill_mbx_attr(&attr, idx, UBASE_MB_QUERY_JFS_CONTEXT, 0);

	ret = ubase_hw_upgrade_ctx_ex(adev, &attr, mailbox);
	if (ret)
		goto out;

	ctx = (struct unic_jfs_ctx *)mailbox->buf;
	if (!check_fd(ctx))
		ret = -EBUSY;

out:
	ubase_free_cmd_mailbox(adev, mailbox);
	return ret;
}

static void unic_multi_jfs_wait_flush(struct unic_dev *unic_dev, u32 num,
				      u16 timeout_ms, u32 start_idx)
{
#define UNIC_WAIT_FLUSH_EVERY_STEP_TIME	50
#define UNIC_WAIT_TIME_AFTER_FLUSH_DONE	100

	struct auxiliary_device *adev = unic_dev->comdev.adev;
	unsigned long end_jiffies, fd_bitmap = 0;
	struct unic_channel *c;
	bool timeout = false;
	u32 i, fd_cnt = 0;
	int ret;

	end_jiffies = jiffies + msecs_to_jiffies(timeout_ms);
	while (!timeout) {
		/* check the last result after waiting enough timeout */
		if (time_is_before_eq_jiffies(end_jiffies))
			timeout = true;

		for (i = 0; i < num; i++) {
			c = &unic_dev->channels.c[i];
			if (test_bit(i, &fd_bitmap))
				continue;

			ret = unic_check_jfs_flush_done(adev, i + start_idx,
							unic_jfs_flush_cqe_done);
			if (ret)
				continue;

			fd_cnt++;
			set_bit(i, &fd_bitmap);
		}

		if (fd_cnt == num)
			break;

		msleep(UNIC_WAIT_FLUSH_EVERY_STEP_TIME);
	}

	for (i = 0; i < num; i++) {
		c = &unic_dev->channels.c[i];
		if (test_bit(i, &fd_bitmap))
			continue;

		ret = unic_check_jfs_flush_done(adev, i + start_idx,
						unic_jfs_flush_ssn_vld);
		if (ret)
			dev_err(adev->dev.parent,
				"wait jfs(%u) flush timeout, ret=%d.\n",
				i + start_idx, ret);
	}

	udelay(UNIC_WAIT_TIME_AFTER_FLUSH_DONE);
}

static void unic_multi_jfs_flush_prepare(struct unic_dev *unic_dev, u32 num,
					 u32 timeout_ms, u32 start_idx)
{
#define UNIC_WAIT_EVERY_STEP_TIME	50U

	struct auxiliary_device *adev = unic_dev->comdev.adev;
	unsigned long end_jiffies, fd_bitmap = 0;
	struct unic_channel *c;
	bool timeout = false;
	u32 i, fd_cnt = 0;

	end_jiffies = jiffies + msecs_to_jiffies(timeout_ms);
	while (!timeout) {
		/* check the last result after waiting enough timeout */
		if (time_is_before_eq_jiffies(end_jiffies))
			timeout = true;

		for (i = 0; i < num; i++) {
			if (test_bit(i, &fd_bitmap))
				continue;

			c = &unic_dev->channels.c[i];
			c->ret = unic_jfs_flush_prepare(adev, i + start_idx);
			if (c->ret)
				continue;

			fd_cnt++;
			set_bit(i, &fd_bitmap);
		}

		if (fd_cnt == num)
			return;

		msleep(UNIC_WAIT_EVERY_STEP_TIME);
	}

	for (i = 0; i < num; i++) {
		c = &unic_dev->channels.c[i];
		if (!test_bit(i, &fd_bitmap))
			dev_err(adev->dev.parent,
				"wait jfs(%u) flush prepare timeout, ret=%d.\n",
				i + start_idx, c->ret);
	}
}

static inline void unic_modify_multi_jfs_state(struct auxiliary_device *adev,
					       u32 num, u32 start_idx)
{
	u32 i;

	for (i = 0; i < num; i++)
		unic_modify_jfs_state_to_error(adev, i + start_idx);
}

static void unic_mbx_destroy_jfs_context(struct auxiliary_device *adev, u32 idx)
{
	struct ubase_cmd_mailbox *mailbox;
	struct ubase_mbx_attr attr = {0};
	int ret;

	mailbox = ubase_alloc_cmd_mailbox(adev);
	if (IS_ERR_OR_NULL(mailbox)) {
		dev_err(adev->dev.parent,
			"failed to alloc mbx for destroy jfs context, i=%u.\n",
			idx);
		goto out;
	}

	ubase_fill_mbx_attr(&attr, idx, UBASE_MB_DESTROY_JFS_CONTEXT, 0);
	ret = ubase_hw_upgrade_ctx_ex(adev, &attr, mailbox);
	if (ret)
		dev_err(adev->dev.parent,
			"failed to post destroy jfs ctx mbx, ret=%d, i=%u.\n",
			ret, idx);

out:
	ubase_free_cmd_mailbox(adev, mailbox);
}

static inline void unic_destroy_multi_jfs_context(struct auxiliary_device *adev,
						  u32 num, u32 start_idx)
{
	u32 i;

	for (i = 0; i < num; i++)
		unic_mbx_destroy_jfs_context(adev, i + start_idx);
}

static void unic_destroy_multi_jfs(struct unic_dev *unic_dev, u32 num,
				   u32 start_idx)
{
	struct auxiliary_device *adev = unic_dev->comdev.adev;
	u32 timeout_ms;

#if defined(UNIC_FPGA_COMPILE)
	timeout_ms = unic_get_ta_timeout_ms(UNIC_TIMEOUT_64S);
#else
	timeout_ms = unic_get_ta_timeout_ms(UNIC_TIMEOUT_8S);
#endif

	unic_multi_jfs_flush_prepare(unic_dev, num, timeout_ms, start_idx);

	unic_modify_multi_jfs_state(adev, num, start_idx);

	unic_multi_jfs_wait_flush(unic_dev, num, timeout_ms, start_idx);

	unic_destroy_multi_jfs_context(adev, num, start_idx);
}

static int unic_sq_alloc_resource(struct auxiliary_device *adev,
				  struct unic_sq *sq, u32 sqebb_depth)
{
	u32 size = sqebb_depth * sizeof(struct unic_sqebb);

	sq->skbs = devm_kcalloc(&adev->dev, sqebb_depth,
				sizeof(struct sk_buff *), GFP_KERNEL);
	if (!sq->skbs) {
		dev_err(adev->dev.parent, "failed to alloc unic sq skb buff.\n");
		return -ENOMEM;
	}

	sq->sqebb = dma_alloc_coherent(adev->dev.parent, size,
				       &sq->sqebb_dma_addr, GFP_KERNEL);
	if (!sq->sqebb) {
		dev_err(adev->dev.parent, "failed to dma alloc unic sqebb.\n");
		goto err_alloc_unic_sqebb;
	}

	return 0;

err_alloc_unic_sqebb:
	devm_kfree(&adev->dev, sq->skbs);

	return -ENOMEM;
}

static void unic_sq_free_resource(struct auxiliary_device *adev,
				  struct unic_sq *sq, u32 sqebb_depth)
{
	u32 size = sqebb_depth * sizeof(struct unic_sqebb);
	u16 sqebb_mask = unic_get_sqe_mask(sq);

	unic_flush_unused_sqe(sq, sqebb_mask, &sq->ci);
	dma_free_coherent(adev->dev.parent, size, sq->sqebb, sq->sqebb_dma_addr);
	devm_kfree(&adev->dev, sq->skbs);
}

int unic_create_sq(struct unic_dev *unic_dev, u32 idx)
{
	struct auxiliary_device *adev = unic_dev->comdev.adev;
	struct ubase_resource_space *mem_base = ubase_get_mem_base(adev);
	struct ubase_adev_caps *unic_caps = ubase_get_unic_caps(adev);
	struct unic_channel *channel = &unic_dev->channels.c[idx];
	struct unic_sq *sq;
	u32 jfs_start_idx;
	u16 sqebb_depth;
	u32 offset;
	int ret;

	if (!unic_caps) {
		dev_err(adev->dev.parent, "failed to get unic caps.\n");
		return -ENODATA;
	}

	if (!mem_base) {
		dev_err(adev->dev.parent, "failed to get mem base.\n");
		return -ENODATA;
	}

	sq = devm_kzalloc(&adev->dev, sizeof(*sq), GFP_KERNEL);
	if (!sq) {
		dev_err(adev->dev.parent, "failed to alloc sq.\n");
		return -ENOMEM;
	}

	sqebb_depth = unic_dev->channels.sqebb_depth;
	ret = unic_sq_alloc_resource(adev, sq, sqebb_depth);
	if (ret)
		goto err_alloc_res;

	jfs_start_idx = unic_caps->jfs.start_idx;
	sq->queue_index = idx & U16_MAX;

	/* The jfs doorbell offset must be consistent with that of the chip. */
	offset = UNIC_JFS_DB_4K_OFFSET;
	sq->db_addr = mem_base->addr + UNIC_JFS_DB_BASE_OFFSET +
		      (idx + jfs_start_idx) * offset;

	sq->netdev = unic_dev->comdev.netdev;
	sq->parent_dev = adev->dev.parent;
	sq->check_ci_late = false;

	unic_init_jfs_ctx(unic_dev, sq, idx, unic_dev->tid);

	ret = unic_mbx_create_jfs_context(adev, sq, idx + jfs_start_idx);
	if (ret)
		goto err_mbx_create_jfs_context;

	channel->sq = sq;
	return 0;

err_mbx_create_jfs_context:
	unic_sq_free_resource(adev, sq, sqebb_depth);
err_alloc_res:
	devm_kfree(&adev->dev, sq);
	return ret;
}

static void unic_free_multi_sq_resource(struct unic_dev *unic_dev, u32 num)
{
	struct auxiliary_device *adev = unic_dev->comdev.adev;
	struct unic_channel *channel;
	u32 i;

	for (i = 0; i < num; i++) {
		channel = &unic_dev->channels.c[i];
		unic_sq_free_resource(adev, channel->sq,
				      unic_dev->channels.sqebb_depth);
		devm_kfree(&adev->dev, channel->sq);
		channel->sq = NULL;
	}
}

void unic_destroy_sq(struct unic_dev *unic_dev, u32 num)
{
	struct auxiliary_device *adev = unic_dev->comdev.adev;
	struct ubase_adev_caps *unic_caps = ubase_get_unic_caps(adev);
	u32 jfs_start_idx;

	if (!num || !unic_caps)
		return;

	jfs_start_idx = unic_caps->jfs.start_idx;

	if (!__unic_resetting(unic_dev))
		unic_destroy_multi_jfs(unic_dev, num, jfs_start_idx);

	unic_free_multi_sq_resource(unic_dev, num);
}

#ifdef CONFIG_UB_UNIC_UBL
static int unic_apply_ub_pkt(struct unic_dev *unic_dev, struct unic_sq *sq,
			     struct sk_buff *skb)
{
	struct ublhdr *ubl = (struct ublhdr *)skb->data;

	if (unic_dev_ubl_supported(unic_dev)) {
		if (unlikely(ubl->cfg == UB_NOIP_CFG_TYPE &&
			     unic_is_port_down(unic_dev))) {
			unic_sq_stats_inc(sq, cfg5_drop_cnt);
			return -EIO;
		}

		if (skb->protocol == htons(ETH_P_IP) ||
		    skb->protocol == htons(ETH_P_IPV6))
			ubl->h_cc = htons(UNIC_CC_DEFAULT_FECN_MODE);

		ubl_rmv_sw_ctype(skb);
	}

	return 0;
}
#endif

static u32 unic_tx_need_sge_num(struct sk_buff *skb)
{
	u32 sge_num = 1, i, size;
	skb_frag_t *frag;

	if (likely(skb->len <= UNIC_SGE_MAX_PAYLOAD && !skb_has_frag_list(skb)))
		return skb_shinfo(skb)->nr_frags + 1U;

	for (i = 0; i < skb_shinfo(skb)->nr_frags; i++) {
		frag = &skb_shinfo(skb)->frags[i];
		size = skb_frag_size(frag);
		if (!size)
			continue;

		sge_num++;
	}

	return sge_num;
}

static int unic_maybe_stop_tx(struct net_device *netdev, struct unic_sq *sq,
			      u32 sge_num)
{
	u32 sqebb_num = unic_sqebb_cnt(sge_num + UNIC_SQE_CTRL_SECTION_NUM);
	struct unic_dev *unic_dev = netdev_priv(netdev);
	u16 spare_num = unic_get_spare_sqebb(sq);

	if (unlikely(sge_num > UNIC_SQE_MAX_SGE_NUM)) {
		unic_sq_stats_inc(sq, over_max_sge_num);
		return -ENOMEM;
	}

	if (likely(spare_num >= sqebb_num))
		return 0;

	netif_stop_subqueue(netdev, sq->queue_index);
	smp_mb(); /* Memory barrier before checking sqebb space */

	if (unlikely(netif_carrier_ok(netdev) &&
		     !test_bit(UNIC_STATE_DOWN, &unic_dev->state) &&
		     unic_get_spare_sqebb(sq) > sqebb_num)) {
		netif_start_subqueue(netdev, sq->queue_index);
		return 0;
	}

	unic_sq_stats_inc(sq, busy);

	return -EBUSY;
}

static void unic_fill_ctrl_owner(struct unic_sq *sq,
				 struct unic_sqe_ctrl_section *ctrl)
{
	struct unic_dev *unic_dev = netdev_priv(sq->netdev);
	u16 sqebb_shift = unic_dev->channels.sqebb_shift;

	ctrl->owner = ~(sq->pi >> sqebb_shift & 0x1);
}

static void unic_fill_ctrl_l3_info(struct unic_sqe_ctrl_section *ctrl,
				   struct sk_buff *skb, struct unic_sq *sq)
{
#define be32_to_le32(x) cpu_to_le32(be32_to_cpu(x))

	struct ipv6hdr *ip6_hdr;

	if (skb->protocol == htons(ETH_P_IP)) {
		ctrl->l3_type = UNIC_L3_TYPE_IPV4;
		ctrl->dip_type = UNIC_L3_TYPE_IPV4;
		ctrl->dip0 = be32_to_le32(ip_hdr(skb)->daddr);
	} else if (skb->protocol == htons(ETH_P_IPV6)) {
		ip6_hdr = ipv6_hdr(skb);
		ctrl->l3_type = UNIC_L3_TYPE_IPV6;
		ctrl->dip_type = UNIC_L3_TYPE_IPV6;
		ctrl->dip0 = be32_to_le32(ip6_hdr->daddr.s6_addr32[3]);
		ctrl->dip1 = be32_to_le32(ip6_hdr->daddr.s6_addr32[2]);
		ctrl->dip2 = be32_to_le32(ip6_hdr->daddr.s6_addr32[1]);
		ctrl->dip3 = be32_to_le32(ip6_hdr->daddr.s6_addr32[0]);
	} else {
		ctrl->l3_type = UNIC_L3_TYPE_NON_IP;
	}
}

static u8 unic_get_l4_protocol(struct sk_buff *skb, struct unic_sq *sq)
{
	u8 l4_proto, *l4hdr, *exthdr;
	union l3_hdr_info l3hdr;
	__be16 frag_off;

	l3hdr.hdr = skb_network_header(skb);
	l4hdr = skb_transport_header(skb);

	if (skb->protocol == htons(ETH_P_IP)) {
		l4_proto = l3hdr.v4->protocol;
	} else {
		exthdr = l3hdr.hdr + sizeof(*l3hdr.v6);
		l4_proto = l3hdr.v6->nexthdr;
		if (l4hdr != exthdr)
			ipv6_skip_exthdr(skb, exthdr - skb->data, &l4_proto,
					 &frag_off);
	}

	return l4_proto;
}

static void unic_set_l2(struct sk_buff *skb, struct unic_sqe_ctrl_section *ctrl,
			u8 *l2_hdr, u8 *l3_hdr, struct unic_dev *unic_dev)
{
#define UNIC_L2_LEN_SUPPLEMENT		4

	u32 l2_len = l3_hdr - l2_hdr;

	/* skb->data not include the first four bytes of the ub link header.
	 * But l2_len need include the length of complete ub link.
	 */
	if (unic_dev_ubl_supported(unic_dev))
		l2_len += UNIC_L2_LEN_SUPPLEMENT;

	/* compute L2 header size, defined in 2 Bytes */
	ctrl->l2hdr_len = cpu_to_le32(l2_len >> UNIC_HEADER_LEN_2B_OFFSET);
}

static void unic_set_l3(struct sk_buff *skb, struct unic_sqe_ctrl_section *ctrl,
			union l3_hdr_info l3, u8 *l4_hdr)
{
	u32 l3_len = l4_hdr - l3.hdr;

	/* compute L3 header size, defined in 4 Bytes */
	ctrl->l3hdr_len = cpu_to_le32(l3_len >> UNIC_HEADER_LEN_4B_OFFSET);

	if (l3.v4->version == IP_VERSION_IPV4)
		ctrl->l3_csum = skb_is_gso(skb) ? UNIC_CSUM_OFFLOAD_ENABLE :
						  UNIC_CSUM_OFFLOAD_DISABLE;
}

static int unic_set_l4(struct sk_buff *skb, struct unic_sqe_ctrl_section *ctrl,
		       union l4_hdr_info l4_hdr, u8 l4_proto,
		       struct unic_dev *unic_dev)
{
	switch (l4_proto) {
	case IPPROTO_TCP:
		ctrl->l4_csum = UNIC_CSUM_OFFLOAD_ENABLE;
		ctrl->l4_type = UNIC_L4_TYPE_TCP;
		ctrl->l4hdr_len = l4_hdr.tcp->doff;
		break;
	case IPPROTO_UDP:
		ctrl->l4_csum = UNIC_CSUM_OFFLOAD_ENABLE;
		ctrl->l4_type = UNIC_L4_TYPE_UDP;
		/* compute inner/normal header size, defined in 4 Bytes */
		ctrl->l4hdr_len = sizeof(*l4_hdr.udp) >>
				  UNIC_HEADER_LEN_4B_OFFSET;
		break;
	default:
		/* drop the skb tunnel packet if hardware don't support,
		 * because hardware can't calculate csum when TSO.
		 */
		if (skb_is_gso(skb)) {
			unic_err(unic_dev,
				 "unknown l4 header tso packets hecksum offload.\n");
			return -EDOM;
		}
		/* the stack computes the IP header already,
		 * driver calculate l4 checksum when not TSO.
		 */
		return skb_checksum_help(skb);
	}

	return 0;
}

static int unic_set_l2l3l4(struct sk_buff *skb, u8 l4_proto,
			   struct unic_sqe_ctrl_section *ctrl,
			   struct net_device *netdev)
{
	struct unic_dev *unic_dev = netdev_priv(netdev);
	u8 *l2_hdr = skb->data;
	union l4_hdr_info l4;
	union l3_hdr_info l3;

	l3.hdr = skb_network_header(skb);
	l4.hdr = skb_transport_header(skb);

	unic_set_l2(skb, ctrl, l2_hdr, l3.hdr, unic_dev);
	unic_set_l3(skb, ctrl, l3, l4.hdr);
	return unic_set_l4(skb, ctrl, l4, l4_proto, unic_dev);
}

static int unic_handle_csum_partial(struct unic_sqe_ctrl_section *ctrl,
				    struct sk_buff *skb, struct unic_sq *sq)
{
	struct net_device *netdev = sq->netdev;
	u8 l4_proto;
	int ret;

	if (skb->ip_summed != CHECKSUM_PARTIAL)
		return 0;

	l4_proto = unic_get_l4_protocol(skb, sq);
	ret = unic_set_l2l3l4(skb, l4_proto, ctrl, netdev);
	if (unlikely(ret < 0)) {
		unic_sq_stats_inc(sq, csum_err);
		return ret;
	}

	return 0;
}

static int unic_handle_vlan_info(struct unic_sq *sq, struct sk_buff *skb)
{
	struct vlan_ethhdr *vhdr;
	int ret;

	if (!(skb->protocol == htons(ETH_P_8021Q) || skb_vlan_tag_present(skb)))
		return 0;

	ret = skb_cow_head(skb, 0);
	if (unlikely(ret < 0)) {
		unic_sq_stats_inc(sq, vlan_err);
		return ret;
	}

	vhdr = skb_vlan_eth_hdr(skb);
	vhdr->h_vlan_TCI |= cpu_to_be16((skb->priority << VLAN_PRIO_SHIFT) &
					VLAN_PRIO_MASK);

	skb->protocol = vlan_get_protocol(skb);

	return 0;
}

static int unic_handle_ctrl_section(struct sk_buff *skb, struct unic_sq *sq,
				    struct unic_sqe_ctrl_section *ctrl,
				    u8 sge_num)
{
	int ret;

	ret = unic_handle_vlan_info(sq, skb);
	if (unlikely(ret))
		goto vlan_err;

	unic_fill_ctrl_l3_info(ctrl, skb, sq);

	ret = unic_handle_csum_partial(ctrl, skb, sq);
	if (unlikely(ret))
		goto csum_err;

	unic_fill_ctrl_owner(sq, ctrl);

	ctrl->sge_num = sge_num;

	return 0;

csum_err:
	memset(ctrl, 0, sizeof(*ctrl));
vlan_err:
	return ret;
}

static inline void unic_fill_sge_section(struct unic_sqe_sge_section *sge, u64 addr,
					 u32 length, u32 tid)
{
	sge->length = length;
	sge->token_id = tid;
	sge->address = addr;
}

static inline struct unic_sqe_sge_section *
unic_get_next_sge(u8 *sec_num, struct unic_sqe_sge_section *sge,
		  struct unic_sq *sq, u16 sqebb_mask)
{
	(*sec_num)++;
	if (*sec_num >= UNIC_SQEBB_MAX_SGE_NUM) {
		sq->pi++;
		*sec_num = 0;
		sge = (struct unic_sqe_sge_section *)
		      &sq->sqebb[sq->pi & sqebb_mask];
	} else {
		sge++;
	}

	return sge;
}

static int unic_map_skb_frags(struct unic_sq *sq, struct sk_buff *skb,
			      u8 *skb_map_num, u8 *sec_num,
			      struct unic_sqe_sge_section *sge)
{
	struct net_device *netdev = sq->netdev;
	u16 sqebb_mask = unic_get_sqe_mask(sq);
	struct unic_dev *unic_dev;
	struct device *dev;
	skb_frag_t *frag;
	dma_addr_t dma;
	u32 size, i;

	unic_dev = netdev_priv(netdev);
	dev = &unic_dev->comdev.adev->dev;

	for (i = 0; i < skb_shinfo(skb)->nr_frags; i++) {
		frag = &skb_shinfo(skb)->frags[i];
		size = skb_frag_size(frag);
		if (!size)
			continue;

		dma = skb_frag_dma_map(dev->parent, frag, 0, size, DMA_TO_DEVICE);
		if (unlikely(dma_mapping_error(dev, dma))) {
			unic_sq_stats_inc(sq, map_err);
			return -ENOMEM;
		}

		sge = unic_get_next_sge(sec_num, sge, sq, sqebb_mask);
		unic_fill_sge_section(sge, (u64)dma, size, unic_dev->tid);

		(*skb_map_num)++;
	}

	return 0;
}

static void unic_clear_sge_section(struct unic_sq *sq, u16 idx, u8 skb_map_num,
				   u16 sqebb_mask)
{
	struct unic_sqe_ctrl_section *ctrl;
	u16 index = idx & sqebb_mask;
	u8 sqebb_cnt;

	ctrl = (struct unic_sqe_ctrl_section *)&sq->sqebb[index];
	sqebb_cnt = unic_sqebb_cnt(skb_map_num + UNIC_SQE_CTRL_SECTION_NUM);
	if (unlikely(index + sqebb_cnt - 1 > sqebb_mask)) {
		memset(ctrl, 0, (sqebb_mask - index + 1) * sizeof(struct unic_sqebb));
		memset(&sq->sqebb[0], 0,
		       (index + sqebb_cnt - 1 - sqebb_mask) * sizeof(struct unic_sqebb));
	} else {
		memset(ctrl, 0, sqebb_cnt * sizeof(struct unic_sqebb));
	}
}

static int unic_handle_sge_section(struct unic_sq *sq, struct sk_buff *skb,
				   struct unic_sqe_sge_section *sge,
				   u16 sqebb_mask)
{
	u8 sec_num = UNIC_SQE_CTRL_SECTION_NUM;
	struct net_device *netdev = sq->netdev;
	struct unic_dev *unic_dev;
	struct device *dev;
	u8 skb_map_num = 0;
	u16 pi = sq->pi;
	dma_addr_t dma;
	u32 size;

	size = skb_headlen(skb);
	unic_dev = netdev_priv(netdev);
	dev = &unic_dev->comdev.adev->dev;

	dma = dma_map_single(dev->parent, skb->data, size, DMA_TO_DEVICE);
	if (unlikely(dma_mapping_error(dev, dma))) {
		unic_sq_stats_inc(sq, map_err);
		return -ENOMEM;
	}

	unic_fill_sge_section(sge, (u64)dma, size, unic_dev->tid);
	skb_map_num++;

	if (unlikely(unic_map_skb_frags(sq, skb, &skb_map_num, &sec_num, sge)))
		goto dma_map_err;

	sq->pi = pi;
	return 0;

dma_map_err:
	unic_unmap_skb_buffer(sq, pi, skb_map_num, sqebb_mask);
	unic_clear_sge_section(sq, pi, skb_map_num, sqebb_mask);
	sq->pi = pi;
	return -ENOMEM;
}

static int unic_handle_sqe(struct sk_buff *skb, struct unic_sq *sq, u8 sge_num)
{
	u16 sqebb_mask = unic_get_sqe_mask(sq);
	struct unic_sqe_ctrl_section *ctrl;
	struct unic_sqe_sge_section *sge;
	int ret;

	ctrl = (struct unic_sqe_ctrl_section *)&sq->sqebb[sq->pi & sqebb_mask];
	ret = unic_handle_ctrl_section(skb, sq, ctrl, sge_num);
	if (unlikely(ret))
		return ret;

	sge = (struct unic_sqe_sge_section *)(ctrl + 1);
	ret = unic_handle_sge_section(sq, skb, sge, sqebb_mask);
	if (unlikely(ret))
		return ret;

	sq->skbs[sq->pi & sqebb_mask] = skb;

	return ret;
}

static void unic_tx_doorbell(struct unic_sq *sq, u16 sge_num, bool doorbell)
{
	u16 sqebb_num = unic_sqebb_cnt(sge_num + UNIC_SQE_CTRL_SECTION_NUM);
	u16 sqebb_mask = unic_get_sqe_mask(sq);
	struct unic_jfs_db jfs_db = {0};

	trace_unic_tx_sqe(sq, sqebb_num, sqebb_mask, doorbell);

	sq->pi += sqebb_num;

	if (!doorbell) {
		unic_sq_stats_inc(sq, more);
		return;
	}

	jfs_db.pi = cpu_to_le16(sq->pi);
	writel(*(u32 *)&jfs_db, sq->db_addr);
	sq->last_pi = sq->pi;
}

static void unic_tx_compensate_doorbell(struct unic_sq *sq)
{
	struct unic_jfs_db jfs_db = {0};

	if (sq->last_pi == sq->pi)
		return;

	trace_unic_tx_sqe(sq, 0, unic_get_sqe_mask(sq), true);
	jfs_db.pi = cpu_to_le16(sq->pi);
	writel(*(u32 *)&jfs_db, sq->db_addr);
	sq->last_pi = sq->pi;
}

netdev_tx_t unic_start_xmit(struct sk_buff *skb, struct net_device *netdev)
{
	struct unic_dev *unic_dev = netdev_priv(netdev);
	struct netdev_queue *dev_queue;
	struct unic_sq *sq;
	bool doorbell;
	u32 sge_num;
	int ret;

	sq = unic_dev->channels.c[skb->queue_mapping].sq;

	if (!unic_dev_ubl_supported(unic_dev) &&
	    skb_put_padto(skb, UNIC_MIN_TX_LEN)) {
		unic_sq_stats_inc(sq, pad_err);
		goto xmit_drop_pkt;
	}

	/* Prefetch the data used later */
	prefetch(skb->data);

	sge_num = unic_tx_need_sge_num(skb);
	ret = unic_maybe_stop_tx(netdev, sq, sge_num);
	if (unlikely(ret < 0)) {
		if (ret == -EBUSY) {
			unic_tx_compensate_doorbell(sq);
			return NETDEV_TX_BUSY;
		}

		unic_err(unic_dev, "unic stop tx, ret = %d.\n", ret);
		goto xmit_drop_pkt;
	}

#ifdef CONFIG_UB_UNIC_UBL
	if (unlikely(unic_apply_ub_pkt(unic_dev, sq, skb)))
		goto xmit_drop_pkt;
#endif

	ret = unic_handle_sqe(skb, sq, sge_num);
	if (unlikely(ret))
		goto xmit_drop_pkt;

	dev_queue = netdev_get_tx_queue(netdev, sq->queue_index);
	doorbell = __netdev_tx_sent_queue(dev_queue, skb->len,
					  netdev_xmit_more());

	unic_tx_doorbell(sq, sge_num, doorbell);

	return NETDEV_TX_OK;

xmit_drop_pkt:
	unic_tx_compensate_doorbell(sq);
	dev_kfree_skb_any(skb);
	return NETDEV_TX_OK;
}

void unic_clear_sq(struct unic_sq *sq)
{
	unic_reclaim_sq_space(sq, 0, NULL, NULL, true);
	sq->start_pi = sq->pi;
	sq->check_ci_late = true;
}

void unic_reset_tx_queue(struct net_device *netdev)
{
	struct unic_dev *unic_dev = netdev_priv(netdev);
	struct netdev_queue *dev_queue;
	struct unic_sq *sq;
	u32 i;

	if (!unic_dev->channels.c)
		return;

	for (i = 0; i < unic_dev->channels.num; i++) {
		sq = unic_dev->channels.c[i].sq;
		dev_queue = netdev_get_tx_queue(netdev, sq->queue_index);
		netdev_tx_reset_queue(dev_queue);
	}
}

void unic_dump_sq_stats(struct net_device *netdev, u32 queue_idx)
{
	struct netdev_queue *queue = netdev_get_tx_queue(netdev, queue_idx);
	struct unic_dev *unic_dev = netdev_priv(netdev);
	struct unic_sq_stats *sq_stats;
	struct unic_channel *channel;
	struct unic_sq *sq;

	channel = &unic_dev->channels.c[queue_idx];
	sq = channel->sq;
	sq_stats = &sq->stats;

	unic_info(unic_dev,
		  "tx timeout, queue index: %u, state: %lu\n"
		  "sq->pi:           %u\n"
		  "sq->ci:           %u\n"
		  "pad_err:          %llu\n"
		  "bytes:            %llu\n"
		  "packets:          %llu\n"
		  "map_err:          %llu\n"
		  "busy:             %llu\n"
		  "more:             %llu\n"
		  "restart_queue:    %llu\n"
		  "over_max_sge_num: %llu\n"
		  "csum_err:         %llu\n"
		  "ci_mismatch:      %llu\n"
		  "fd_cnt:           %llu\n"
		  "drop_cnt:         %llu\n"
		  "cfg5_drop_cnt:    %llu\n",
		  queue_idx, queue->state, sq->pi, sq->ci,
		  sq_stats->pad_err, sq_stats->bytes, sq_stats->packets,
		  sq_stats->map_err, sq_stats->busy, sq_stats->more,
		  sq_stats->restart_queue, sq_stats->over_max_sge_num,
		  sq_stats->csum_err, sq_stats->ci_mismatch, sq_stats->fd_cnt,
		  sq_stats->drop_cnt, sq_stats->cfg5_drop_cnt);
}

void unic_mask_key_words(void *sqebb)
{
	struct unic_sqe_ctrl_section *ctrl;
	struct unic_sqe_sge_section *sge;
	u16 i;

	ctrl = (struct unic_sqe_ctrl_section *)sqebb;
	sge = (struct unic_sqe_sge_section *)(ctrl + 1);
	for (i = 0; i < ctrl->sge_num; i++) {
		sge->address = 0;
		sge++;
	}
}

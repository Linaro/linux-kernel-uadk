// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved.
 *
 */

#include <linux/delay.h>

#include "ubase_dev.h"
#include "ubase_trace.h"
#include "ubase_cmd.h"
#include "ubase_ctrlq.h"

static inline void ubase_ctrlq_crq_table_init(struct ubase_dev *udev)
{
	struct ubase_ctrlq_crq_table *crq_tab = &udev->ctrlq.crq_table;

	mutex_init(&crq_tab->lock);
	INIT_LIST_HEAD(&crq_tab->crq_nbs.list);
}

static inline void ubase_ctrlq_crq_table_uninit(struct ubase_dev *udev)
{
	struct ubase_ctrlq_crq_table *crq_tab = &udev->ctrlq.crq_table;

	mutex_destroy(&crq_tab->lock);
}

static inline u16 ubase_ctrlq_msg_queue_depth(struct ubase_dev *udev)
{
	return udev->ctrlq.csq.depth << 1;
}

static inline u16 ubase_ctrlq_max_seq(struct ubase_dev *udev)
{
	return ubase_ctrlq_msg_queue_depth(udev) - 1;
}

static int ubase_ctrlq_msg_queue_init(struct ubase_dev *udev)
{
	u16 msg_ctx_size = sizeof(struct ubase_ctrlq_msg_ctx);
	u16 depth = ubase_ctrlq_msg_queue_depth(udev);
	struct ubase_ctrlq_msg_ctx *ctx;
	u16 i;

	udev->ctrlq.msg_queue = kzalloc(depth * msg_ctx_size, GFP_KERNEL);
	if (!udev->ctrlq.msg_queue) {
		ubase_err(udev, "failed to alloc ctrlq msg queue.\n");
		return -ENOMEM;
	}

	for (i = 0; i < depth; i++) {
		ctx = &udev->ctrlq.msg_queue[i];
		init_completion(&ctx->done);
	}

	return 0;
}

static void ubase_ctrlq_msg_queue_uninit(struct ubase_dev *udev)
{
	kfree(udev->ctrlq.msg_queue);
	udev->ctrlq.msg_queue = NULL;
}

static int ubase_ctrlq_map_queue(struct ubase_dev *udev)
{
	struct ubase_ctrlq_ring *csq = &udev->ctrlq.csq;
	struct ubase_ctrlq_ring *crq = &udev->ctrlq.crq;
	u32 addr_h, addr_l;
	u64 queue_addr;
	size_t size;

	if (!ubase_dev_ctrlq_supported(udev))
		return 0;

	addr_h = ubase_read_dev(&udev->hw, UBASE_CTRLQ_CSQ_BASEADDR_H_REG);
	addr_l = ubase_read_dev(&udev->hw, UBASE_CTRLQ_CSQ_BASEADDR_L_REG);
	queue_addr = ubase_addr_gen(addr_h, addr_l);
	size = csq->depth * UBASE_CTRLQ_BB_LEN;
	csq->base_addr = devm_ioremap(udev->dev, queue_addr, size);
	if (!csq->base_addr) {
		ubase_err(udev, "failed to map ctrlq csq base addr, size = %lu.\n",
			  size);
		return -ENOMEM;
	}

	addr_h = ubase_read_dev(&udev->hw, UBASE_CTRLQ_CRQ_BASEADDR_H_REG);
	addr_l = ubase_read_dev(&udev->hw, UBASE_CTRLQ_CRQ_BASEADDR_L_REG);
	queue_addr = ubase_addr_gen(addr_h, addr_l);
	size = crq->depth * UBASE_CTRLQ_BB_LEN;
	crq->base_addr = devm_ioremap(udev->dev, queue_addr, size);
	if (!crq->base_addr) {
		ubase_err(udev, "failed to map ctrlq crq base addr, size = %lu.\n",
			  size);
		goto err_map_crq;
	}

	return 0;

err_map_crq:
	devm_iounmap(udev->dev, csq->base_addr);
	csq->base_addr = NULL;
	return -ENOMEM;
}

static void ubase_ctrlq_unmap_queue(struct ubase_dev *udev)
{
	struct ubase_ctrlq_ring *csq = &udev->ctrlq.csq;
	struct ubase_ctrlq_ring *crq = &udev->ctrlq.crq;

	if (!ubase_dev_ctrlq_supported(udev))
		return;

	if (csq->base_addr) {
		devm_iounmap(udev->dev, csq->base_addr);
		csq->base_addr = NULL;
	}

	if (crq->base_addr) {
		devm_iounmap(udev->dev, crq->base_addr);
		crq->base_addr = NULL;
	}
}

static void ubase_ctrlq_queue_pi_ci_init(struct ubase_dev *udev)
{
	struct ubase_ctrlq_ring *csq = &udev->ctrlq.csq;
	struct ubase_ctrlq_ring *crq = &udev->ctrlq.crq;
	u16 crq_hw_pi;

	if (!ubase_dev_ctrlq_supported(udev))
		return;

	csq->pi = ubase_read_dev(&udev->hw, UBASE_CTRLQ_CSQ_TAIL_REG);
	csq->ci = ubase_read_dev(&udev->hw, UBASE_CTRLQ_CSQ_HEAD_REG);

	crq_hw_pi = ubase_read_dev(&udev->hw, UBASE_CTRLQ_CRQ_TAIL_REG);
	ubase_write_dev(&udev->hw, UBASE_CTRLQ_CRQ_HEAD_REG, crq_hw_pi);
	crq->pi = crq_hw_pi;
	crq->ci = crq_hw_pi;
}

static int ubase_query_ctrlq_queue_info(struct ubase_dev *udev)
{
#define UBASE_CTRLQ_QUEUE_DEFAULT	2048

	struct ubase_ctrlq_ring *csq = &udev->ctrlq.csq;
	struct ubase_ctrlq_ring *crq = &udev->ctrlq.crq;

	csq->depth = UBASE_CTRLQ_QUEUE_DEFAULT;
	crq->depth = UBASE_CTRLQ_QUEUE_DEFAULT;

	return 0;
}

static int ubase_ctrlq_get_queue_depth(struct ubase_dev *udev)
{
#define ubase_reg_val_to_depth(a) ((a) << 3)

	struct ubase_ctrlq_ring *csq = &udev->ctrlq.csq;
	struct ubase_ctrlq_ring *crq = &udev->ctrlq.crq;
	u16 reg_val;

	if (!ubase_dev_ctrlq_supported(udev))
		return ubase_query_ctrlq_queue_info(udev);

	reg_val = (u16)ubase_read_dev(&udev->hw, UBASE_CTRLQ_CSQ_DEPTH_REG);
	csq->depth = ubase_reg_val_to_depth(reg_val);
	if (!csq->depth) {
		ubase_err(udev, "the csq depth is 0.\n");
		return -EINVAL;
	}

	reg_val = (u16)ubase_read_dev(&udev->hw, UBASE_CTRLQ_CRQ_DEPTH_REG);
	crq->depth = ubase_reg_val_to_depth(reg_val);
	if (!crq->depth) {
		ubase_err(udev, "the crq depth is 0.\n");
		return -EINVAL;
	}

	return 0;
}

static int ubase_ctrlq_queue_init(struct ubase_dev *udev)
{
#define CTRLQ_TX_TIMEOUT 3000

	struct ubase_ctrlq_ring *csq = &udev->ctrlq.csq;
	struct ubase_ctrlq_ring *crq = &udev->ctrlq.crq;
	int ret;

	spin_lock_init(&csq->lock);
	spin_lock_init(&crq->lock);

	ret = ubase_ctrlq_get_queue_depth(udev);
	if (ret) {
		ubase_err(udev, "failed to get queue depth, ret = %d.\n",
			  ret);
		return ret;
	}

	csq->tx_timeout = CTRLQ_TX_TIMEOUT;
	ubase_ctrlq_queue_pi_ci_init(udev);

	ret = ubase_ctrlq_map_queue(udev);
	if (ret)
		ubase_err(udev, "failed to map ctrlq queue, ret = %d.\n",
			  ret);

	return ret;
}

static void ubase_ctrlq_queue_uninit(struct ubase_dev *udev)
{
	ubase_ctrlq_unmap_queue(udev);
}

int ubase_ctrlq_init(struct ubase_dev *udev)
{
	int ret;

	if (test_bit(UBASE_STATE_RST_HANDLING_B, &udev->state_bits)) {
		ubase_ctrlq_queue_pi_ci_init(udev);
		goto success;
	}

	ret = ubase_ctrlq_queue_init(udev);
	if (ret)
		return ret;

	ret = ubase_ctrlq_msg_queue_init(udev);
	if (ret)
		goto err_msg_queue_init;

	udev->ctrlq.csq_next_seq = 1;
	atomic_set(&udev->ctrlq.req_cnt, 0);
	ubase_ctrlq_crq_table_init(udev);

success:
	set_bit(UBASE_CTRLQ_STATE_ENABLE, &udev->ctrlq.state);
	return 0;

err_msg_queue_init:
	ubase_ctrlq_queue_uninit(udev);
	return ret;
}

static void ubase_ctrlq_clean_msg_queue(struct ubase_dev *udev)
{
	struct ubase_ctrlq_ring *csq = &udev->ctrlq.csq;
	u16 depth = ubase_ctrlq_msg_queue_depth(udev);
	struct ubase_ctrlq_msg_ctx *ctx;
	u16 i;

	spin_lock_bh(&csq->lock);
	for (i = 0; i < depth; i++) {
		ctx = &udev->ctrlq.msg_queue[i];
		ctx->valid = 0;
	}
	spin_unlock_bh(&csq->lock);
}

static void ubase_ctrlq_clean_pending_msgs(struct ubase_dev *udev)
{
#define UBASE_CTRLQ_CLEAN_WAIT_TIME	5

	struct ubase_ctrlq_ring *csq = &udev->ctrlq.csq;
	u16 depth = ubase_ctrlq_msg_queue_depth(udev);
	struct ubase_ctrlq_msg_ctx *ctx;
	u16 i;

	spin_lock_bh(&csq->lock);
	for (i = 1; i < depth; i++) {
		ctx = &udev->ctrlq.msg_queue[i];
		if (!completion_done(&ctx->done))
			complete(&ctx->done);
	}
	spin_unlock_bh(&csq->lock);

	while (atomic_read(&udev->ctrlq.req_cnt))
		msleep(UBASE_CTRLQ_CLEAN_WAIT_TIME);
}

void ubase_ctrlq_disable(struct ubase_dev *udev)
{
#define UBASE_CTRLQ_CLEAR_WAIT_TIME	5

	clear_bit(UBASE_CTRLQ_STATE_ENABLE, &udev->ctrlq.state);

	/* wait to ensure that the crq completes the possible left
	 * over commands.
	 */
	if (!test_bit(UBASE_STATE_RST_HANDLING_B, &udev->state_bits)) {
		while (test_bit(UBASE_STATE_CTRLQ_HANDLING,
		       &udev->service_task.state))
			msleep(UBASE_CTRLQ_CLEAR_WAIT_TIME);
	}

	ubase_ctrlq_clean_pending_msgs(udev);
}

void ubase_ctrlq_uninit(struct ubase_dev *udev)
{
	if (udev->reset_stage != UBASE_RESET_STAGE_UNINIT)
		ubase_ctrlq_disable(udev);

	if (!test_bit(UBASE_STATE_RST_HANDLING_B, &udev->state_bits)) {
		ubase_ctrlq_crq_table_uninit(udev);
		ubase_ctrlq_msg_queue_uninit(udev);
		ubase_ctrlq_queue_uninit(udev);
	} else {
		ubase_ctrlq_clean_msg_queue(udev);
	}
}

static u16 ubase_ctrlq_calc_bb_num(u16 in_size)
{
	u16 data_size = in_size > UBASE_CTRLQ_DATA_LEN ?
			in_size - UBASE_CTRLQ_DATA_LEN : 0;

	return (DIV_ROUND_UP(data_size, UBASE_CTRLQ_BB_LEN) + 1);
}

static u16 ubase_ctrlq_remain_space(struct ubase_dev *udev)
{
	struct ubase_ctrlq_ring *csq = &udev->ctrlq.csq;
	u16 used;

	used = (csq->pi - csq->ci + csq->depth) % csq->depth;

	return csq->depth - used - 1;
}

static void ubase_ctrlq_fill_first_bb(struct ubase_dev *udev,
				      struct ubase_ctrlq_base_block *head,
				      struct ubase_ctrlq_msg *msg,
				      struct ubase_ctrlq_ue_info *ue_info)
{
	struct ub_entity *ue = to_ub_entity(udev->dev);

	head->service_ver = msg->service_ver;
	head->service_type = msg->service_type;
	head->opcode = msg->opcode;
	head->mbx_ue_id = ue_info ? ue_info->mbx_ue_id : 0;
	head->ret = msg->is_resp ? msg->resp_ret : 0;
	head->bus_ue_id = cpu_to_le16(ue_info ?
				      ue_info->bus_ue_id :
				      ue->entity_idx);
	memcpy(head->data, msg->in, min(msg->in_size, UBASE_CTRLQ_DATA_LEN));
}

static inline void ubase_ctrlq_csq_report_irq(struct ubase_dev *udev)
{
#define UBASE_CTRLQ_CSQ_IRQ_EN	BIT(16)
#define UBASE_CTRLQ_CSQ_IRQ_VEC_IDX	0

	u32 val = UBASE_CTRLQ_CSQ_IRQ_EN | UBASE_CTRLQ_CSQ_IRQ_VEC_IDX;

	ubase_write_reg(udev->hw.rs0_base.addr, 0, val);
}

static int ubase_ctrlq_send_to_cmdq(struct ubase_dev *udev,
				    struct ubase_ctrlq_base_block *head,
				    struct ubase_ctrlq_msg *msg, u8 num)
{
	u32 req_len = msg->in_size + sizeof(struct ubase_ue2ue_ctrlq_head) +
		      UBASE_CTRLQ_HDR_LEN;
	struct ubase_ue2ue_ctrlq_head ue2ue_head = {0};
	u16 seq = le16_to_cpu(head->seq);
	struct ubase_cmd_buf in;
	void *req;
	int ret;

	req = kzalloc(req_len, GFP_ATOMIC);
	if (!req)
		return -ENOMEM;

	ue2ue_head.head.sub_cmd = UBASE_UE2UE_CTRLQ_MSG;
	ue2ue_head.seq = seq;
	ue2ue_head.in_size = msg->in_size;
	ue2ue_head.out_size = msg->out_size;
	ue2ue_head.need_resp = msg->need_resp;
	ue2ue_head.is_resp = msg->is_resp;
	memcpy(req, &ue2ue_head, sizeof(ue2ue_head));
	memcpy((u8 *)req + sizeof(ue2ue_head), head, UBASE_CTRLQ_HDR_LEN);
	memcpy((u8 *)req + sizeof(ue2ue_head) + UBASE_CTRLQ_HDR_LEN, msg->in,
	       msg->in_size);

	__ubase_fill_inout_buf(&in, UBASE_OPC_UE2UE_UBASE, false, req_len, req);
	ret = __ubase_cmd_send_in(udev, &in);
	if (ret)
		ubase_err(udev,
			  "failed to send ue2ue ctrlq msg, seq = %u, ret = %d.\n",
			  seq, ret);

	kfree(req);
	return ret;
}

static void ubase_ctrlq_send_to_csq(struct ubase_dev *udev,
				    struct ubase_ctrlq_base_block *head,
				    struct ubase_ctrlq_msg *msg, u8 num)
{
	struct ubase_ctrlq_ring *csq = &udev->ctrlq.csq;
	u32 total_size = msg->in_size;
	u32 size, offset = 0;
	u8 cnt = 0;
	u8 *addr;

	while (cnt < num) {
		addr = csq->base_addr + csq->pi * UBASE_CTRLQ_BB_LEN;
		if (cnt == 0) {
			memcpy_toio(addr, head, sizeof(*head));
			trace_ubase_ctrlq_csq(udev->dev, num, csq->pi, csq->ci,
					      head, sizeof(*head));
			total_size -= UBASE_CTRLQ_DATA_LEN;
			offset += UBASE_CTRLQ_DATA_LEN;
		} else {
			size = min_t(u32, total_size, UBASE_CTRLQ_BB_LEN);
			memcpy_toio(addr, (u8 *)msg->in + offset, size);
			trace_ubase_ctrlq_csq(udev->dev, num, csq->pi, csq->ci,
					      (u8 *)msg->in + offset, size);
			total_size -= size;
			offset += size;
		}
		csq->pi++;
		if (csq->pi >= csq->depth)
			csq->pi = 0;

		cnt++;
	}

	ubase_write_dev(&udev->hw, UBASE_CTRLQ_CSQ_TAIL_REG, csq->pi);
	ubase_ctrlq_csq_report_irq(udev);
}

static int ubase_ctrlq_send_msg_to_sq(struct ubase_dev *udev,
				      struct ubase_ctrlq_base_block *head,
				      struct ubase_ctrlq_msg *msg, u8 num)
{
	if (ubase_dev_ctrlq_supported(udev)) {
		ubase_ctrlq_send_to_csq(udev, head, msg, num);
		return 0;
	}

	return ubase_ctrlq_send_to_cmdq(udev, head, msg, num);
}

static int ubase_ctrlq_wait_completed(struct ubase_dev *udev, u16 seq,
				      struct ubase_ctrlq_msg *msg)
{
	struct ubase_ctrlq_msg_ctx *ctx = &udev->ctrlq.msg_queue[seq];
	struct ubase_ctrlq_ring *csq = &udev->ctrlq.csq;
	int ret;

	if (!wait_for_completion_timeout(&ctx->done,
					 msecs_to_jiffies(csq->tx_timeout))) {
		ubase_err(udev,
			  "ctrlq wait resp timeout, seq = %u, opcode = 0x%x, service_type = 0x%x.\n",
			  seq, msg->opcode, msg->service_type);
		return -ETIMEDOUT;
	}

	ret = ctx->result;
	if (ret)
		ubase_err(udev,
			  "ctrlq recv failed resp for seq = %u, opcode = 0x%x, service_type = 0x%x, ret = %d.\n",
			  seq, msg->opcode, msg->service_type, ret);

	return -ret;
}

static int ubase_ctrlq_alloc_seq(struct ubase_dev *udev,
				 struct ubase_ctrlq_msg *msg, u16 *seq)
{
	struct ubase_ctrlq_msg_ctx *ctx = udev->ctrlq.msg_queue;
	u16 max_seq = ubase_ctrlq_max_seq(udev);
	u16 next_seq = udev->ctrlq.csq_next_seq;
	u32 i;

	for (i = next_seq; i <= max_seq; i++) {
		if (!ctx[i].valid)
			goto success;
	}

	/* seq 0 is not used. */
	for (i = 1; i < next_seq; i++) {
		if (!ctx[i].valid)
			goto success;
	}

	return -EBUSY;

success:
	*seq = i;
	udev->ctrlq.csq_next_seq = i + 1;

	return 0;
}

static void ubase_ctrlq_free_seq(struct ubase_dev *udev, u16 seq)
{
	struct ubase_ctrlq_msg_ctx *ctx = &udev->ctrlq.msg_queue[seq];
	struct ubase_ctrlq_ring *csq = &udev->ctrlq.csq;

	spin_lock_bh(&csq->lock);
	ctx->valid = 0;
	spin_unlock_bh(&csq->lock);
}

static void ubase_ctrlq_addto_msg_queue(struct ubase_dev *udev, u16 seq,
					struct ubase_ctrlq_msg *msg,
					struct ubase_ctrlq_ue_info *ue_info)
{
	struct ubase_ctrlq_msg_ctx *ctx;

	if (!msg->need_resp)
		return;

	ctx = &udev->ctrlq.msg_queue[seq];
	ctx->valid = 1;
	ctx->is_sync = msg->need_resp && msg->out && msg->out_size ? 1 : 0;
	ctx->result = ETIME;
	ctx->dead_jiffies = jiffies + msecs_to_jiffies(UBASE_CTRLQ_DEAD_TIME);
	if (ctx->is_sync) {
		ctx->out = msg->out;
		ctx->out_size = msg->out_size;
	}

	if (ue_info) {
		ctx->ue_seq = ue_info->seq;
		ctx->bus_ue_id = ue_info->bus_ue_id;
	}
	reinit_completion(&ctx->done);
}

static int ubase_ctrlq_msg_check(struct ubase_dev *udev,
				 struct ubase_ctrlq_msg *msg)
{
	if (!msg || !msg->in || !msg->in_size) {
		ubase_err(udev, "ctrlq input buf is invalid.\n");
		return -EINVAL;
	}

	if (msg->is_resp && !(msg->resp_seq & UBASE_CTRLQ_SEQ_MASK)) {
		ubase_err(udev, "ctrlq input resp_seq(%u) is invalid.\n",
			  msg->resp_seq);
		return -EINVAL;
	}

	if (msg->in_size > UBASE_CTRLQ_MAX_DATA_SIZE) {
		ubase_err(udev,
			  "requested ctrlq space(%u) exceeds the maximum(%u).\n",
			  msg->in_size, UBASE_CTRLQ_MAX_DATA_SIZE);
		return -EINVAL;
	}

	return 0;
}

static int ubase_ctrlq_check_csq_enough(struct ubase_dev *udev, u16 num)
{
	struct ubase_ctrlq_ring *csq = &udev->ctrlq.csq;

	if (!ubase_dev_ctrlq_supported(udev))
		return 0;

	csq->ci = (u16)ubase_read_dev(&udev->hw, UBASE_CTRLQ_CSQ_HEAD_REG);
	if (num > ubase_ctrlq_remain_space(udev)) {
		ubase_warn(udev,
			   "no enough space in ctrlq, ci = %u, num = %u.\n",
			   csq->ci, num);
		return -EBUSY;
	}

	return 0;
}

static int ubase_ctrlq_send_real(struct ubase_dev *udev,
				 struct ubase_ctrlq_msg *msg,
				 struct ubase_ctrlq_ue_info *ue_info)
{
	bool sync_req = msg->out && msg->out_size && msg->need_resp;
	bool no_resp = !msg->is_resp && !msg->need_resp;
	struct ubase_ctrlq_ring *csq = &udev->ctrlq.csq;
	struct ubase_ctrlq_base_block head = {0};
	u16 seq, num;
	int ret;

	num = ubase_ctrlq_calc_bb_num(msg->in_size);

	spin_lock_bh(&csq->lock);

	ret = ubase_ctrlq_check_csq_enough(udev, num);
	if (ret)
		goto unlock;

	if (!msg->is_resp) {
		ret = ubase_ctrlq_alloc_seq(udev, msg, &seq);
		if (ret) {
			ubase_warn(udev, "no enough seq in ctrlq.\n");
			goto unlock;
		}
	} else {
		seq = msg->resp_seq;
	}

	ubase_ctrlq_addto_msg_queue(udev, seq, msg, ue_info);

	head.bb_num = num;
	head.seq = cpu_to_le16(seq);
	ubase_ctrlq_fill_first_bb(udev, &head, msg, ue_info);
	ret = ubase_ctrlq_send_msg_to_sq(udev, &head, msg, num);
	if (ret) {
		spin_unlock_bh(&csq->lock);
		goto free_seq;
	}

	spin_unlock_bh(&csq->lock);

	if (sync_req)
		ret = ubase_ctrlq_wait_completed(udev, seq, msg);

free_seq:
	/* Only the seqs in synchronous requests and no response requests need to be released. */
	/* The seqs are released in periodic tasks of asynchronous requests. */
	if (sync_req || no_resp)
		ubase_ctrlq_free_seq(udev, seq);
	return ret;
unlock:
	spin_unlock_bh(&csq->lock);
	return ret;
}

int __ubase_ctrlq_send(struct ubase_dev *udev, struct ubase_ctrlq_msg *msg,
		       struct ubase_ctrlq_ue_info *ue_info)
{
#define UBASE_CTRLQ_RETRY_TIMES	3
#define UBASE_RETRY_INTERVAL	100

	int ret, retry_cnt = 0;

	ret = ubase_ctrlq_msg_check(udev, msg);
	if (ret)
		return ret;

	while (retry_cnt++ <= UBASE_CTRLQ_RETRY_TIMES) {
		if (!test_bit(UBASE_CTRLQ_STATE_ENABLE, &udev->ctrlq.state)) {
			ubase_warn(udev, "ctrlq is disabled in csq.\n");
			return -EAGAIN;
		}

		atomic_inc(&udev->ctrlq.req_cnt);
		ret = ubase_ctrlq_send_real(udev, msg, ue_info);
		atomic_dec(&udev->ctrlq.req_cnt);
		if (ret == -ETIMEDOUT && retry_cnt <= UBASE_CTRLQ_RETRY_TIMES) {
			ubase_info(udev,
				   "Ctrlq send msg retry, retry cnt = %d.\n",
				   retry_cnt);
			msleep(UBASE_RETRY_INTERVAL);
		} else {
			break;
		}
	}

	return ret;
}

int ubase_ctrlq_send_msg(struct auxiliary_device *aux_dev,
			 struct ubase_ctrlq_msg *msg)
{
	if (!aux_dev || !msg)
		return -EINVAL;

	return __ubase_ctrlq_send(__ubase_get_udev_by_adev(aux_dev), msg, NULL);
}
EXPORT_SYMBOL(ubase_ctrlq_send_msg);

static bool ubase_ctrlq_crq_is_empty(struct ubase_dev *udev, struct ubase_hw *hw)
{
	udev->ctrlq.crq.pi = ubase_read_dev(hw, UBASE_CTRLQ_CRQ_TAIL_REG);

	return udev->ctrlq.crq.pi == udev->ctrlq.crq.ci;
}

static void ubase_ctrlq_update_crq_ci(struct ubase_dev *udev, u8 bb_num)
{
	struct ubase_ctrlq_ring *crq = &udev->ctrlq.crq;

	crq->ci = (crq->ci + bb_num) % crq->depth;
	ubase_write_dev(&udev->hw, UBASE_CTRLQ_CRQ_HEAD_REG, crq->ci);
}

static void ubase_ctrlq_read_msg_data(struct ubase_dev *udev, u8 num, u8 *msg)
{
	struct ubase_ctrlq_ring *crq = &udev->ctrlq.crq;
	u16 pos = crq->ci;
	u8 i;

	for (i = 0; i < num; i++) {
		memcpy_fromio(msg + i * UBASE_CTRLQ_BB_LEN,
			      (u8 *)crq->base_addr + pos * UBASE_CTRLQ_BB_LEN,
			      UBASE_CTRLQ_BB_LEN);
		trace_ubase_ctrlq_crq(udev->dev, num, crq->pi, crq->ci,
				      msg + i * UBASE_CTRLQ_BB_LEN,
				      UBASE_CTRLQ_BB_LEN);
		pos = (pos + 1) % crq->depth;
	}
}

static void ubase_ctrlq_crq_event_callback(struct ubase_dev *udev,
					   struct ubase_ctrlq_base_block *head,
					   void *msg_data, u16 msg_data_len,
					   u16 seq)
{
	struct ubase_ctrlq_crq_table *crq_tab = &udev->ctrlq.crq_table;
	struct ubase_ctrlq_crq_event_nbs *nbs;

	mutex_lock(&crq_tab->lock);
	list_for_each_entry(nbs, &crq_tab->crq_nbs.list, list) {
		if (nbs->crq_nb.service_type == head->service_type &&
		    nbs->crq_nb.opcode == head->opcode) {
			nbs->crq_nb.crq_handler(nbs->crq_nb.back,
						head->service_ver,
						msg_data,
						msg_data_len,
						seq);
			break;
		}
	}
	mutex_unlock(&crq_tab->lock);
}

static void ubase_ctrlq_notify_completed(struct ubase_dev *udev,
					 struct ubase_ctrlq_base_block *head,
					 u16 seq, void *msg, u16 msg_len)
{
	struct ubase_ctrlq_msg_ctx *ctx;

	ctx = &udev->ctrlq.msg_queue[seq];
	ctx->result = head->ret;
	memcpy(ctx->out, msg, min(msg_len, ctx->out_size));

	complete(&ctx->done);
}

static bool ubase_ctrlq_check_seq(struct ubase_dev *udev, u16 seq)
{
	bool is_pushed = !!(seq & UBASE_CTRLQ_SEQ_MASK);
	u16 max_seq = ubase_ctrlq_max_seq(udev);

	return is_pushed || (seq && seq <= max_seq);
}

void ubase_ctrlq_handle_crq_msg(struct ubase_dev *udev,
				struct ubase_ctrlq_base_block *head,
				u16 seq, void *msg_data, u16 data_len)
{
	bool is_pushed = !!(seq & UBASE_CTRLQ_SEQ_MASK);
	struct ubase_ctrlq_ring *csq = &udev->ctrlq.csq;
	struct ubase_ctrlq_msg_ctx *ctx;

	if (!is_pushed) {
		spin_lock_bh(&csq->lock);
		ctx = &udev->ctrlq.msg_queue[seq];
		if (!ctx->valid) {
			ubase_warn(udev,
				   "seq is invalid, opcode = 0x%x, service_type = 0x%x, seq = %u.\n",
				   head->opcode, head->service_type, seq);
			goto unlock;
		}
		if (ctx->is_sync) {
			ubase_ctrlq_notify_completed(udev, head, seq, msg_data,
						     data_len);
			goto unlock;
		}
		ctx->valid = 0;
		spin_unlock_bh(&csq->lock);
	}

	ubase_ctrlq_crq_event_callback(udev, head, msg_data, data_len, seq);
	return;

unlock:
	spin_unlock_bh(&csq->lock);
}

static void ubase_ctrlq_handle_self_msg(struct ubase_dev *udev,
					struct ubase_ctrlq_base_block *head)
{
	u16 seq = le16_to_cpu(head->seq);
	u16 msg_len, data_len;
	void *msg;

	msg_len = head->bb_num * UBASE_CTRLQ_BB_LEN;
	msg = kzalloc(msg_len, GFP_KERNEL);
	if (!msg) {
		ubase_err(udev,
			  "failed to alloc ctrlq crq msg data, opcode = 0x%x, service_type = 0x%x, seq = %u.\n",
			  head->opcode, head->service_type, seq);
		return;
	}

	ubase_ctrlq_read_msg_data(udev, head->bb_num, msg);
	data_len = msg_len - UBASE_CTRLQ_HDR_LEN;

	ubase_ctrlq_handle_crq_msg(udev, head, seq,
				   (u8 *)msg + UBASE_CTRLQ_HDR_LEN, data_len);

	kfree(msg);
}

static void ubase_ctrlq_handle_other_msg(struct ubase_dev *udev,
					 struct ubase_ctrlq_base_block *head)
{
	u16 resp_len, seq = le16_to_cpu(head->seq);
	bool is_pushed = !!(seq & UBASE_CTRLQ_SEQ_MASK);
	struct ubase_ctrlq_ring *csq = &udev->ctrlq.csq;
	struct ubase_ue2ue_ctrlq_head *ue2ue_head;
	struct ubase_ctrlq_msg_ctx ctx = {0};
	struct ubase_cmd_buf in;
	void *resp, *msg;
	int ret;

	if (!is_pushed) {
		spin_lock_bh(&csq->lock);
		ctx = udev->ctrlq.msg_queue[seq];
		if (!ctx.valid) {
			ubase_warn(udev, "invalid seq = %u, opcode = 0x%x, service_type = 0x%x.\n",
				   seq, head->opcode, head->service_type);
			spin_unlock_bh(&csq->lock);
			return;
		}
		if (!ctx.is_sync)
			udev->ctrlq.msg_queue[seq].valid = 0;
		spin_unlock_bh(&csq->lock);
	}

	resp_len = head->bb_num * UBASE_CTRLQ_BB_LEN +
		   sizeof(struct ubase_ue2ue_ctrlq_head);
	resp = kzalloc(resp_len, GFP_KERNEL);
	if (!resp) {
		ubase_err(udev, "failed to alloc resp mem, seq = %u.\n", seq);
		return;
	}

	msg = (u8 *)resp + sizeof(struct ubase_ue2ue_ctrlq_head);
	ubase_ctrlq_read_msg_data(udev, head->bb_num, msg);

	ue2ue_head = (struct ubase_ue2ue_ctrlq_head *)resp;
	ue2ue_head->head.sub_cmd = UBASE_UE2UE_CTRLQ_MSG;
	ue2ue_head->head.bus_ue_id = is_pushed ? head->bus_ue_id :
				     cpu_to_le16(ctx.bus_ue_id);
	ue2ue_head->seq = is_pushed ? seq : ctx.ue_seq;
	__ubase_fill_inout_buf(&in, UBASE_OPC_UE2UE_UBASE, false, resp_len, resp);
	ret = __ubase_cmd_send_in(udev, &in);
	if (ret)
		ubase_warn(udev,
			   "failed to send ue ctrlq msg, opc = 0x%x, service_type = 0x%x, ret = %d.\n",
			   head->opcode, head->service_type, ret);

	kfree(resp);
}

static inline void ubase_ctrlq_reset_crq_ci(struct ubase_dev *udev)
{
	struct ubase_ctrlq_ring *crq = &udev->ctrlq.crq;

	crq->pi = ubase_read_dev(&udev->hw, UBASE_CTRLQ_CRQ_TAIL_REG);
	crq->ci = crq->pi;
	ubase_write_dev(&udev->hw, UBASE_CTRLQ_CRQ_HEAD_REG, crq->ci);
}

void ubase_ctrlq_crq_handler(struct ubase_dev *udev)
{
	struct ubase_ctrlq_ring *crq = &udev->ctrlq.crq;
	struct ub_entity *ue = to_ub_entity(udev->dev);
	struct ubase_ctrlq_base_block head = {0};
	u8 bb_num;
	u8 *addr;
	u16 seq;

	while (!ubase_ctrlq_crq_is_empty(udev, &udev->hw)) {
		if (!test_bit(UBASE_CTRLQ_STATE_ENABLE, &udev->ctrlq.state)) {
			ubase_warn(udev, "ctrlq is disabled in crq.\n");
			return;
		}

		addr = crq->base_addr + crq->ci * UBASE_CTRLQ_BB_LEN;
		memcpy_fromio(&head, addr, UBASE_CTRLQ_HDR_LEN);
		seq = le16_to_cpu(head.seq);
		bb_num = head.bb_num;

		if (unlikely(!bb_num || bb_num > UBASE_CTRLQ_MAX_BB)) {
			ubase_err(udev, "ctrlq crq bb_num(%u) is invalid.\n",
				  bb_num);
			ubase_ctrlq_reset_crq_ci(udev);
			return;
		}

		if (!ubase_ctrlq_check_seq(udev, seq)) {
			ubase_warn(udev,
				   "ctrlq recv invalid seq, seq = %u.\n", seq);
			ubase_ctrlq_update_crq_ci(udev, bb_num);
			continue;
		}

		if (le16_to_cpu(head.bus_ue_id) == ue->entity_idx)
			ubase_ctrlq_handle_self_msg(udev, &head);
		else
			ubase_ctrlq_handle_other_msg(udev, &head);

		ubase_ctrlq_update_crq_ci(udev, bb_num);
	}
}

void ubase_ctrlq_service_task(struct ubase_delay_work *ubase_work)
{
	struct ubase_dev *udev = container_of(ubase_work, struct ubase_dev,
				 service_task);
	struct ubase_ctrlq_crq_table *crq_tab = &udev->ctrlq.crq_table;

	if (!test_and_clear_bit(UBASE_STATE_CTRLQ_SERVICE_SCHED,
				&udev->service_task.state) ||
		test_and_set_bit(UBASE_STATE_CTRLQ_HANDLING,
				 &udev->service_task.state))
		return;

	if (time_is_before_eq_jiffies(crq_tab->last_crq_scheduled +
				      UBASE_CTRLQ_SCHED_TIMEOUT))
		ubase_warn(udev,
			   "ctrlq crq service task is scheduled after %ums on cpu%d!\n",
			   jiffies_to_msecs(jiffies - crq_tab->last_crq_scheduled),
			   smp_processor_id());

	ubase_ctrlq_crq_handler(udev);

	clear_bit(UBASE_STATE_CTRLQ_HANDLING, &udev->service_task.state);
}

void ubase_ctrlq_clean_service_task(struct ubase_delay_work *ubase_work)
{
	struct ubase_dev *udev = container_of(ubase_work, struct ubase_dev,
				 service_task);
	struct ubase_ctrlq_ring *csq = &udev->ctrlq.csq;
	u16 i, max_seq = ubase_ctrlq_max_seq(udev);
	struct ubase_ctrlq_msg_ctx *ctx;

	if (!test_bit(UBASE_CTRLQ_STATE_ENABLE, &udev->ctrlq.state) ||
	    ubase_dev_pmu_supported(udev))
		return;

	spin_lock_bh(&csq->lock);
	for (i = 1; i <= max_seq; i++) {
		ctx = &udev->ctrlq.msg_queue[i];
		if (ctx->valid && time_is_before_eq_jiffies(ctx->dead_jiffies))
			ctx->valid = 0;
	}
	spin_unlock_bh(&csq->lock);
}

int ubase_ctrlq_register_crq_event(struct auxiliary_device *aux_dev,
				   struct ubase_ctrlq_event_nb *nb)
{
	struct ubase_ctrlq_crq_event_nbs *nbs, *tmp, *new_nbs;
	struct ubase_ctrlq_crq_table *crq_tab;
	struct ubase_dev *udev;
	int ret;

	if (!aux_dev || !nb || !nb->crq_handler)
		return -EINVAL;

	udev = __ubase_get_udev_by_adev(aux_dev);
	crq_tab = &udev->ctrlq.crq_table;
	mutex_lock(&crq_tab->lock);
	list_for_each_entry_safe(nbs, tmp, &crq_tab->crq_nbs.list, list) {
		if (nbs->crq_nb.service_type == nb->service_type &&
		    nbs->crq_nb.opcode == nb->opcode) {
			ret = -EEXIST;
			goto err_crq_register;
		}
	}

	new_nbs = kzalloc(sizeof(*new_nbs), GFP_KERNEL);
	if (!new_nbs) {
		ret = -ENOMEM;
		goto err_crq_register;
	}

	new_nbs->crq_nb = *nb;
	list_add_tail(&new_nbs->list, &crq_tab->crq_nbs.list);
	mutex_unlock(&crq_tab->lock);

	return 0;

err_crq_register:
	mutex_unlock(&crq_tab->lock);
	ubase_err(udev,
		  "failed to register ctrlq crq event, opcode = 0x%x, service_type = 0x%x, ret = %d.\n",
		  nb->opcode, nb->service_type, ret);

	return ret;
}
EXPORT_SYMBOL(ubase_ctrlq_register_crq_event);

void ubase_ctrlq_unregister_crq_event(struct auxiliary_device *aux_dev,
				      u8 service_type, u8 opcode)
{
	struct ubase_ctrlq_crq_event_nbs *nbs, *tmp;
	struct ubase_ctrlq_crq_table *crq_tab;
	struct ubase_dev *udev;

	if (!aux_dev)
		return;

	udev = __ubase_get_udev_by_adev(aux_dev);
	crq_tab = &udev->ctrlq.crq_table;
	mutex_lock(&crq_tab->lock);
	list_for_each_entry_safe(nbs, tmp, &crq_tab->crq_nbs.list, list) {
		if (nbs->crq_nb.service_type == service_type &&
		    nbs->crq_nb.opcode == opcode) {
			list_del(&nbs->list);
			kfree(nbs);
			break;
		}
	}
	mutex_unlock(&crq_tab->lock);
}
EXPORT_SYMBOL(ubase_ctrlq_unregister_crq_event);

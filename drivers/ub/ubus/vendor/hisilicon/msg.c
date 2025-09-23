// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) HiSilicon Technologies Co., Ltd. 2025. All rights reserved.
 */

#define pr_fmt(fmt) "ubus hisi msg: " fmt

#include <linux/delay.h>

#include "../../ubus.h"
#include "../../msg.h"
#include "../../enum.h"
#include "vdm.h"
#include "hisi-msg.h"

struct hi_msg_sqe_pld {
	char packet[HI_MSG_SQE_PLD_SIZE];
};

struct hi_message_device {
	struct hi_msg_core hmc;
	struct hi_cqe_state *cqe_state;
	atomic_t msg_in_flight_cnt;
	struct message_device mdev;
};

#define to_hi_message_device(dev) \
	container_of(dev, struct hi_message_device, mdev)

enum hi_cq_sw_state { CQ_SW_INIT, CQ_SW_HANDLED };

struct hi_cqe_state {
	u8 state;
};
#define HI_CQE_STATE_SZ sizeof(struct hi_cqe_state)

static inline void cqe_state_set(struct hi_message_device *hmd, int idx,
				 u8 state)
{
	hmd->cqe_state[idx].state = state;
}
#define cqe_state_get(hmd, idx) ((hmd)->cqe_state[idx].state)

#define MSG_MAX (HI_SQ_CFG_DEPTH - 1)
#define q_left_cnt(q) ((q)->depth - q_used_cnt((q)) - 1)
#define sq_sw_left(hmd) (MSG_MAX - atomic_read(&(hmd)->msg_in_flight_cnt))
#define sq_pld_entry_off(hmd, idx) \
	((HI_MSG_SQE_SIZE * HI_SQ_CFG_DEPTH) + (HI_MSG_SQE_PLD_SIZE * (idx)))
#define sq_pld_entry_sw(hmd, idx)                                   \
	((void *)&((hmd)->hmc.queue[MSG_SQ].sqe[HI_SQ_CFG_DEPTH]) + \
	 (HI_MSG_SQE_PLD_SIZE * (idx)))

#define MSGQ_TIMEOUT (HZ * msg_wait / 1000)

#define MSN_SIZE 65535
DEFINE_SPINLOCK(msn_msg_lock);
DECLARE_BITMAP(msn_msg, MSN_SIZE);
DEFINE_SPINLOCK(msn_enum_lock);
DECLARE_BITMAP(msn_enum, MSN_SIZE);
DEFINE_SPINLOCK(msn_private_lock);
DECLARE_BITMAP(msn_private, MSN_SIZE);

/* Return 0 represents failure, return 1-65535 represents success */
static u16 hi_msn_get(int type)
{
	unsigned long flags, find, tmp, size, *addr;
	static u16 msn[TASK_TYPE_NUM] = {};
	spinlock_t *lock;
	u16 ret;

	find = msn[type];

	if (type == PROTOCOL_MSG) {
		addr = msn_msg;
		lock = &msn_msg_lock;
	} else if (type == PROTOCOL_ENUM) {
		addr = msn_enum;
		lock = &msn_enum_lock;
	} else { /* hisi private */
		addr = msn_private;
		lock = &msn_private_lock;
	}

	spin_lock_irqsave(lock, flags);
	if (unlikely(test_bit(find, addr))) {
		size = MSN_SIZE;
		tmp = find;
		find = find_next_zero_bit(addr, size, find);
		if (unlikely(find == size)) {
			size = tmp;
			find = find_next_zero_bit(addr, size, 0);
			if (unlikely(find == size)) {
				ret = 0;
				goto out;
			}
		}
	}

	set_bit(find, addr);
	msn[type] = (find + 1) % MSN_SIZE;
	ret = find + 1; /* find bit is 0~65534, ret use 1~65535 */
out:
	spin_unlock_irqrestore(lock, flags);
	return ret;
}

static void hi_msn_put(int type, u16 msn)
{
	unsigned long flags, *addr;
	spinlock_t *lock;

	if (msn == 0 || type >= TASK_TYPE_NUM) {
		pr_warn("invalid msn or type\n");
		return;
	}

	if (type == PROTOCOL_MSG) {
		addr = msn_msg;
		lock = &msn_msg_lock;
	} else if (type == PROTOCOL_ENUM) {
		addr = msn_enum;
		lock = &msn_enum_lock;
	} else { /* hisi private */
		addr = msn_private;
		lock = &msn_private_lock;
	}

	spin_lock_irqsave(lock, flags);
	clear_bit(msn - 1, addr);
	spin_unlock_irqrestore(lock, flags);
}

static int hi_msg_get_sq_idle_num(struct hi_message_device *hmd, int num,
				  bool tx)
{
	struct hi_msg_queue *sq = &hmd->hmc.queue[MSG_SQ];
	int real, sw_left, hw_left;

	hw_left = q_left_cnt(sq);
	sw_left = sq_sw_left(hmd);
	real = num;

	/* Assure message in flight not overflow, just tx need check */
	if (tx && real > sw_left)
		real = sw_left;

	/* If not enough, update ci by read hardware */
	if (real > hw_left) {
		sq->ci = hi_msg_reg_read(&hmd->hmc, SQ_CI);
		hw_left = q_left_cnt(sq);
		if (real > hw_left)
			real = hw_left;
	}

	return real;
}

static int hi_msg_sq_submit(struct hi_message_device *hmd,
			    struct hi_msg_sqe *sqe, struct hi_msg_sqe_pld *pld,
			    int num, bool wait)
{
	struct hi_msg_queue *sq = &hmd->hmc.queue[MSG_SQ];
	struct hi_msg_core *hmc = &hmd->hmc;
	struct hi_msg_sqe *tar_sqe;
	unsigned long flags;
	int i, real;
	u16 sqe_idx;

	spin_lock_irqsave(&sq->lock, flags);

	real = hi_msg_get_sq_idle_num(hmd, num, wait);
	if (!real) {
		spin_unlock_irqrestore(&sq->lock, flags);
		return 0;
	}

	for (i = 0; i < real; i++) {
		sqe_idx = q_ptr_idx(sq, pi, i);
		tar_sqe = sq->sqe + sqe_idx;
		(sqe + i)->p_addr = sq_pld_entry_off(hmd, sqe_idx);
		memcpy(tar_sqe, sqe + i, HI_MSG_SQE_SIZE);
		memcpy(sq_pld_entry_sw(hmd, sqe_idx), pld[i].packet,
		       (sqe + i)->p_len);
	}

	sq->pi = q_ptr_idx(sq, pi, real);
	wmb(); /* Ensure the register is written correctly. */
	hi_msg_reg_write(hmc, SQ_PI, sq->pi);
	if (wait)
		atomic_add(real, &hmd->msg_in_flight_cnt);

	spin_unlock_irqrestore(&sq->lock, flags);

	return real;
}

static int hi_msg_sync_wait(struct hi_message_device *hmd, int task_type,
			    u16 msn, u64 duration, bool flag)
{
#define SLEEP_MIN_US 1000
#define SLEEP_MAX_US 2000
	u64 end_time = get_jiffies_64() + duration;
	struct hi_msg_core *hmc = &hmd->hmc;
	int idx;

	while (!time_after64(get_jiffies_64(), end_time)) {
		idx = hi_msg_cq_poll(hmc, task_type, msn);
		if (idx >= 0)
			return idx;

		if (flag)
			usleep_range(SLEEP_MIN_US, SLEEP_MAX_US);
	}

	dev_err(hmc->dev, "task %d msn %#x wait cqe timeout\n", task_type, msn);
	return -ETIMEDOUT;
}

static void hi_msg_cq_update(struct hi_message_device *hmd)
{
	struct hi_msg_queue *cq = &hmd->hmc.queue[MSG_CQ];
	struct hi_msg_core *hmc = &hmd->hmc;
	int i, cnt, idx, release_cnt = 0;
	struct hi_msg_cqe *cqe;
	unsigned long flags;

	spin_lock_irqsave(&cq->lock, flags);
	if (cq->ci == cq->pi) {
		spin_unlock_irqrestore(&cq->lock, flags);
		return;
	}

	cnt = q_used_cnt(cq);
	for (i = 0; i < cnt; i++) {
		idx = q_ptr_idx(cq, ci, i);
		if (cqe_state_get(hmd, idx) != CQ_SW_HANDLED)
			break;

		cqe = cq_entry(hmc, idx);
		if (cqe->task_type != PROTOCOL_MSG || cqe->type == MSG_RSP) {
			hi_msn_put(cqe->task_type, cqe->msn);
			release_cnt++;
		}

		cqe_state_set(hmd, idx, CQ_SW_INIT);
	}
	if (i == 0) {
		spin_unlock_irqrestore(&cq->lock, flags);
		return;
	}

	hi_msg_rq_update(hmc, q_ptr_idx(cq, ci, i - 1));

	cq->ci = q_ptr_idx(cq, ci, i);
	wmb(); /* Ensure the register is written correctly. */
	hi_msg_reg_write(hmc, CQ_CI, cq->ci);
	atomic_sub(release_cnt, &hmd->msg_in_flight_cnt);
	spin_unlock_irqrestore(&cq->lock, flags);
}

static int hi_msg_queue_init(struct hi_message_device *hmd)
{
	struct hi_msg_core *hmc = &hmd->hmc;
	size_t size;
	int ret;

	ret = hi_msg_core_init(hmc, MSGQ_USER_BUS_DRV);
	if (ret)
		return ret;

	size = HI_CQE_STATE_SZ * hmc->queue[MSG_CQ].depth;
	hmd->cqe_state = kzalloc(size, GFP_KERNEL);
	if (!hmd->cqe_state) {
		dev_err(hmc->dev, "cqe state memory failed\n");
		ret = -ENOMEM;
		goto cqe_state_fail;
	}
	return 0;

cqe_state_fail:
	hi_msg_core_uninit(hmc);
	return ret;
}

static void hi_msg_queue_uninit(struct hi_message_device *hmd)
{
	kfree(hmd->cqe_state);
	hi_msg_core_uninit(&hmd->hmc);
}

static int hi_message_probe_dev(struct ub_entity *uent)
{
	return 0;
}

static void hi_message_remove_dev(struct ub_entity *uent)
{
}

static bool pkt_plen_valid(void *pkt, u16 pkt_size, int task_type)
{
	struct msg_pkt_header *header = (struct msg_pkt_header *)pkt;

	if (pkt_size > HI_MSG_SQE_PLD_SIZE) {
		pr_err("pkt_size %#x over\n", pkt_size);
		return false;
	}

	if (task_type == PROTOCOL_MSG &&
	    ((pkt_size < MSG_PKT_HEADER_SIZE) ||
	     (header->msgetah.plen > PLD_SIZE_MAX) ||
	     (header->msgetah.plen != (pkt_size - MSG_PKT_HEADER_SIZE)))) {
		pr_err("msg plen %#x, pkt_size %#x invalid\n",
		       header->msgetah.plen, pkt_size);
		return false;
	}

	if (task_type == PROTOCOL_ENUM &&
	    ((pkt_size < ENUM_PKT_HEADER_SIZE) ||
	     (pkt_size - ENUM_PKT_HEADER_SIZE > PLD_SIZE_MAX))) {
		pr_err("enum pkt_size %#x invalid\n", pkt_size);
		return false;
	}

	return true;
}

static int hi_message_sync(struct message_device *mdev, struct msg_info *info,
			   int task_type, u8 code, bool flag)
{
	struct hi_message_device *hmd = to_hi_message_device(mdev);
	struct hi_msg_core *hmc = &hmd->hmc;
	struct hi_msg_sqe sqe = {};
	struct hi_msg_cqe *cqe;
	int ret, msn, cnt;
	int cq_idx;

	if (!pkt_plen_valid(info->req_packet, info->req_pkt_size, task_type))
		return -EINVAL;

	msn = hi_msn_get(task_type);
	if (msn == 0) {
		dev_err(hmc->dev, "task type %d alloc msn failed\n", task_type);
		return -ENOSPC;
	}

	hi_msg_sqe_init(&sqe, msn, info, task_type, code);
	hi_msg_set_pkt_msn(info, task_type, msn, hmc->user);

	cnt = hi_msg_sq_submit(
		hmd, &sqe, (struct hi_msg_sqe_pld *)info->req_packet, 1, true);
	if (cnt != 1) {
		dev_err(hmc->dev, "sq submit failed\n");
		hi_msn_put(task_type, msn);
		return -ENOSPC;
	}

	cq_idx = hi_msg_sync_wait(hmd, task_type, msn, MSGQ_TIMEOUT, flag);
	if (cq_idx < 0) {
		if (cq_idx == -ENOMEM)
			hi_msn_put(task_type, msn);
		return cq_idx;
	}

	cqe = cq_entry(hmc, cq_idx);
	ret = hi_message_cqe_check(hmc->dev, &sqe, cqe, info->rsp_pkt_size);
	if (!ret) {
		hi_msg_rqe_get(hmc, info->rsp_packet, cqe);
		info->actual_rsp_size = cqe->p_len;
	}

	cqe_state_set(hmd, cq_idx, CQ_SW_HANDLED);
	hi_msg_cq_update(hmd);
	return ret;
}

int hi_message_sync_request(struct message_device *mdev, struct msg_info *info,
			    u8 code)
{
	return hi_message_sync(mdev, info, PROTOCOL_MSG, code, false);
}

static int hi_message_sync_enum(struct message_device *mdev,
				struct msg_info *info, u8 cmd)
{
	return hi_message_sync(mdev, info, PROTOCOL_ENUM, cmd, false);
}

static int hi_message_response(struct message_device *mdev,
			       struct msg_info *info, u8 code)
{
	struct hi_message_device *hmd = to_hi_message_device(mdev);
	struct hi_msg_sqe sqe = {};
	struct msg_pkt_header *header;
	void *pkt = info->req_packet;
	int cnt;

	if (!pkt_plen_valid(info->req_packet, info->req_pkt_size, PROTOCOL_MSG))
		return -EINVAL;

	header = (struct msg_pkt_header *)pkt;
	hi_msg_sqe_init(&sqe, header->src_tassn, info, PROTOCOL_MSG, code);

	cnt = hi_msg_sq_submit(
		hmd, &sqe, (struct hi_msg_sqe_pld *)info->req_packet, 1, false);
	if (cnt != 1) {
		dev_err(hmd->hmc.dev, "async sq submit failed\n");
		return -ENOSPC;
	}

	return 0;
}

static int hi_message_send(struct message_device *mdev, struct msg_info *info,
			   u8 code)
{
	struct hi_message_device *hmd = to_hi_message_device(mdev);
	struct hi_msg_core *hmc = &hmd->hmc;
	struct hi_msg_sqe sqe = {};
	int msn, cnt;
	int ret = 0;

	if (!pkt_plen_valid(info->req_packet, info->req_pkt_size, PROTOCOL_MSG))
		return -EINVAL;

	msn = hi_msn_get(PROTOCOL_MSG);
	if (msn == 0) {
		dev_err(hmc->dev, "alloc msn failed\n");
		return -ENOSPC;
	}

	hi_msg_sqe_init(&sqe, msn, info, PROTOCOL_MSG, code);
	hi_msg_set_pkt_msn(info, PROTOCOL_MSG, msn, hmc->user);

	cnt = hi_msg_sq_submit(
		hmd, &sqe, (struct hi_msg_sqe_pld *)info->req_packet, 1, false);
	if (cnt != 1) {
		dev_err(hmc->dev, "sq submit failed\n");
		ret = -ENOSPC;
	}

	hi_msn_put(PROTOCOL_MSG, msn);
	return ret;
}

/* For some special message need sleep, work in non-atomic context */
int hi_message_sync_request_sched(struct message_device *mdev,
				  struct msg_info *info, u8 code)
{
	return hi_message_sync(mdev, info, PROTOCOL_MSG, code, true);
}

int hi_message_private(struct message_device *mdev, struct msg_info *info,
		       u8 opcode)
{
	return hi_message_sync(mdev, info, HISI_PRIVATE, opcode, false);
}

static struct message_ops hi_message_ops = {
	.probe_dev = hi_message_probe_dev,
	.remove_dev = hi_message_remove_dev,
	.sync_request = hi_message_sync_request,
	.response = hi_message_response,
	.sync_enum = hi_message_sync_enum,
	.vdm_rx_handler = hi_vdm_rx_msg_handler,
	.send = hi_message_send,
};

int hi_msg_device_probe(struct ub_bus_controller *ubc)
{
	struct device *dev = &ubc->dev;
	struct hi_message_device *hmd;
	struct hi_msg_core *hmc;
	int ret;

	hmd = kzalloc(sizeof(*hmd), GFP_KERNEL);
	if (!hmd)
		return -ENOMEM;

	hmc = &hmd->hmc;
	hmc->dev = dev;
	hmc->q_addr = ubc->attr.queue_addr;
	hmc->q_size = HI_MSGQ_SIZE;

	ret = hi_msg_queue_init(hmd);
	if (ret) {
		dev_err(dev, "init message queue failed\n");
		goto queue_init_fail;
	}

	message_device_set_ops(&hmd->mdev, &hi_message_ops);
	message_device_set_fwnode(&hmd->mdev, dev->fwnode);

	ret = message_device_register(&hmd->mdev);
	if (ret) {
		dev_err(dev, "register message device failed\n");
		goto mdev_reg_fail;
	}

	ubc->mdev = &hmd->mdev;

	return 0;

mdev_reg_fail:
	hi_msg_queue_uninit(hmd);
queue_init_fail:
	kfree(hmd);
	return ret;
}

void hi_msg_device_remove(struct ub_bus_controller *ubc)
{
	struct hi_message_device *hmd = to_hi_message_device(ubc->mdev);

	ubc->mdev = NULL;
	message_device_unregister(&hmd->mdev);
	hi_msg_queue_uninit(hmd);
	kfree(hmd);
}

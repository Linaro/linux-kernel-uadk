// SPDX-License-Identifier: GPL-2.0+
/* Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved. */

#define dev_fmt(fmt) "CDMA: " fmt

#include "cdma_common.h"
#include "cdma_mbox.h"
#include "cdma_tp.h"
#include <ub/ubase/ubase_comm_ctrlq.h>

static inline int cdma_ctrlq_msg_send(struct cdma_dev *cdev,
				      struct ubase_ctrlq_msg *msg)
{
	int ret;

	ret = ubase_ctrlq_send_msg(cdev->adev, msg);
	if (ret)
		dev_err(cdev->dev, "ctrlq send msg failed, ret = %d.\n", ret);

	return ret;
}

static void cdma_ctrlq_delete_ctp(struct cdma_dev *cdev, u32 tpn,
				  struct cdma_tp_cfg *cfg)
{
	struct cdma_ctrlq_tp_delete_cfg ctrlq_tp = { 0 };
	struct cdma_ctrlq_tp_ret tp_out = { 0 };
	struct ubase_ctrlq_msg msg = { 0 };
	int ret;

	ctrlq_tp.seid_flag = CDMA_CTRLQ_FLAG_ON;
	ctrlq_tp.deid_flag = CDMA_CTRLQ_FLAG_ON;
	ctrlq_tp.scna = cfg->scna;
	ctrlq_tp.dcna = cfg->dcna;
	ctrlq_tp.seid[0] = cfg->seid;
	ctrlq_tp.deid[0] = cfg->deid;
	ctrlq_tp.tpn = tpn;
	ctrlq_tp.route_type = CDMA_ROUTE_TYPE_CNA;
	ctrlq_tp.trans_type = CDMA_TRANS_TYPE_CDMA_CTP;

	msg.service_ver = UBASE_CTRLQ_SER_VER_01;
	msg.service_type = UBASE_CTRLQ_SER_TYPE_TP_ACL;
	msg.opcode = CDMA_CTRLQ_DELETE_CTP;
	msg.need_resp = CDMA_CTRLQ_FLAG_ON;
	msg.is_resp = CDMA_CTRLQ_FLAG_OFF;
	msg.in_size = sizeof(ctrlq_tp);
	msg.in = &ctrlq_tp;
	msg.out_size = sizeof(tp_out);
	msg.out = &tp_out;

	ret = cdma_ctrlq_msg_send(cdev, &msg);
	if (ret)
		dev_err(cdev->dev,
			"delete ctp failed, tpn = %u, dcna = %u, ret = %d.\n",
			tpn, cfg->dcna, ret);
}

static struct cdma_tp *cdma_id_find_ctp(struct cdma_dev *cdev, u32 id)
{
	struct cdma_tp *tp;

	spin_lock(&cdev->ctp_table.lock);
	tp = idr_find(&cdev->ctp_table.idr_tbl.idr, id);
	if (!tp)
		dev_err(cdev->dev,
			"get tp from table failed, id = %u.\n", id);
	spin_unlock(&cdev->ctp_table.lock);

	return tp;
}

void cdma_delete_ctp(struct cdma_dev *cdev, u32 tp_id)
{
	struct cdma_tp_cfg cfg = { 0 };
	struct cdma_tp *tp;
	bool flag = false;
	u32 tpn;

	if (!cdev)
		return;

	tp = cdma_id_find_ctp(cdev, tp_id);
	if (!tp)
		return;

	spin_lock(&cdev->ctp_table.lock);
	refcount_dec(&tp->refcount);
	if (refcount_dec_if_one(&tp->refcount)) {
		if (cdev->status != CDMA_SUSPEND) {
			flag = true;
			tpn = tp->base.tpn;
			cfg = tp->base.cfg;
		}

		dev_dbg(cdev->dev,
			"refcout of tp %u is equal to one and erased.\n", tp_id);
		idr_remove(&cdev->ctp_table.idr_tbl.idr, tp_id);
		kfree(tp);
	}
	spin_unlock(&cdev->ctp_table.lock);

	if (flag)
		cdma_ctrlq_delete_ctp(cdev, tpn, &cfg);
}

void cdma_destroy_ctp_imm(struct cdma_dev *cdev, u32 tp_id)
{
	struct cdma_tp_cfg cfg = { 0 };
	struct cdma_tp *tp;
	u32 tpn;

	if (!cdev)
		return;

	tp = cdma_id_find_ctp(cdev, tp_id);
	if (!tp)
		return;

	spin_lock(&cdev->ctp_table.lock);
	tpn = tp->base.tpn;
	cfg = tp->base.cfg;
	idr_remove(&cdev->ctp_table.idr_tbl.idr, tp_id);
	kfree(tp);
	spin_unlock(&cdev->ctp_table.lock);

	cdma_ctrlq_delete_ctp(cdev, tpn, &cfg);
}

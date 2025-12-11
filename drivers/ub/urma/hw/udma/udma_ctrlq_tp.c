// SPDX-License-Identifier: GPL-2.0+
/* Copyright(c) 2025 HiSilicon Technologies CO., Ltd. All rights reserved. */

#define dev_fmt(fmt) "UDMA: " fmt

#include <linux/slab.h>
#include <ub/ubase/ubase_comm_dev.h>
#include <ub/ubase/ubase_comm_cmd.h>
#include <ub/ubase/ubase_comm_ctrlq.h>
#include "udma_cmd.h"
#include <ub/urma/udma/udma_ctl.h>
#include "udma_common.h"
#include "udma_ctrlq_tp.h"

static void udma_ctrlq_set_tp_msg(struct ubase_ctrlq_msg *msg, void *in,
				  uint16_t in_len, void *out, uint16_t out_len)
{
	msg->service_ver = UBASE_CTRLQ_SER_VER_01;
	msg->service_type = UBASE_CTRLQ_SER_TYPE_TP_ACL;
	msg->need_resp = 1;
	msg->is_resp = 0;
	msg->in_size = in_len;
	msg->in = in;
	msg->out_size = out_len;
	msg->out = out;
}

int udma_ctrlq_remove_single_tp(struct udma_dev *udev, uint32_t tpn, int status)
{
	struct udma_ctrlq_remove_single_tp_req_data tp_cfg_req = {};
	struct ubase_ctrlq_msg msg = {};
	int r_status = 0;
	int ret;

	tp_cfg_req.tpn = tpn;
	tp_cfg_req.tp_status = (uint32_t)status;
	msg.opcode = UDMA_CMD_CTRLQ_REMOVE_SINGLE_TP;
	udma_ctrlq_set_tp_msg(&msg, (void *)&tp_cfg_req,
			      sizeof(tp_cfg_req), &r_status, sizeof(int));

	ret = ubase_ctrlq_send_msg(udev->comdev.adev, &msg);
	if (ret)
		dev_err(udev->dev, "remove single tp %u failed, ret %d status %d.\n",
			tpn, ret, r_status);

	return ret;
}

static int udma_send_req_to_ue(struct udma_dev *udma_dev, uint8_t ue_idx)
{
	struct ubcore_resp *ubcore_req;
	int ret;

	ubcore_req = kzalloc(sizeof(*ubcore_req), GFP_KERNEL);
	if (!ubcore_req)
		return -ENOMEM;

	ret = send_resp_to_ue(udma_dev, ubcore_req, ue_idx,
			      UDMA_CMD_NOTIFY_UE_FLUSH_DONE);
	if (ret)
		dev_err(udma_dev->dev, "fail to notify ue the tp flush done, ret %d.\n", ret);

	kfree(ubcore_req);

	return ret;
}

static struct udma_ue_idx_table *udma_find_ue_idx_by_tpn(struct udma_dev *udev,
							 uint32_t tpn)
{
	struct udma_ue_idx_table *tp_ue_idx_info;

	xa_lock(&udev->tpn_ue_idx_table);
	tp_ue_idx_info = xa_load(&udev->tpn_ue_idx_table, tpn);
	if (!tp_ue_idx_info) {
		dev_warn(udev->dev, "ue idx info not exist, tpn %u.\n", tpn);
		xa_unlock(&udev->tpn_ue_idx_table);

		return NULL;
	}

	__xa_erase(&udev->tpn_ue_idx_table, tpn);
	xa_unlock(&udev->tpn_ue_idx_table);

	return tp_ue_idx_info;
}

int udma_ctrlq_tp_flush_done(struct udma_dev *udev, uint32_t tpn)
{
	struct udma_ctrlq_tp_flush_done_req_data tp_cfg_req = {};
	struct udma_ue_idx_table *tp_ue_idx_info;
	struct ubase_ctrlq_msg msg = {};
	int ret = 0;
	uint32_t i;

	tp_ue_idx_info = udma_find_ue_idx_by_tpn(udev, tpn);
	if (tp_ue_idx_info) {
		for (i = 0; i < tp_ue_idx_info->num; i++)
			(void)udma_send_req_to_ue(udev, tp_ue_idx_info->ue_idx[i]);

		kfree(tp_ue_idx_info);
	} else {
		ret = udma_open_ue_rx(udev, true, false, false, 0);
		if (ret)
			dev_err(udev->dev, "udma open ue rx failed in tp flush done.\n");
	}

	tp_cfg_req.tpn = tpn;
	msg.opcode = UDMA_CMD_CTRLQ_TP_FLUSH_DONE;
	udma_ctrlq_set_tp_msg(&msg, (void *)&tp_cfg_req, sizeof(tp_cfg_req), NULL, 0);
	ret = ubase_ctrlq_send_msg(udev->comdev.adev, &msg);
	if (ret)
		dev_err(udev->dev, "tp flush done ctrlq tp %u failed, ret %d.\n", tpn, ret);

	return ret;
}

int send_resp_to_ue(struct udma_dev *udma_dev, struct ubcore_resp *req_host,
		    uint8_t dst_ue_idx, uint16_t opcode)
{
	struct udma_resp_msg *udma_req;
	struct ubase_cmd_buf in;
	uint32_t msg_len;
	int ret;

	msg_len = sizeof(*udma_req) + req_host->len;
	udma_req = kzalloc(msg_len, GFP_KERNEL);
	if (!udma_req)
		return -ENOMEM;

	udma_req->dst_ue_idx = dst_ue_idx;
	udma_req->resp_code = opcode;

	(void)memcpy(&udma_req->resp, req_host, sizeof(*req_host));
	(void)memcpy(udma_req->resp.data, req_host->data, req_host->len);

	udma_fill_buf(&in, UBASE_OPC_MUE_TO_UE, false, msg_len, udma_req);

	ret = ubase_cmd_send_in(udma_dev->comdev.adev, &in);
	if (ret)
		dev_err(udma_dev->dev,
			"send resp msg cmd failed, ret is %d.\n", ret);

	kfree(udma_req);

	return ret;
}

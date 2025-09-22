// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) HiSilicon Technologies Co., Ltd. 2025. All rights reserved.
 */

#define pr_fmt(fmt) "ubus hisi vdm: " fmt

#include "../../ubus.h"
#include "../../msg.h"
#include "../../pool.h"
#include "hisi-ubus.h"
#include "vdm.h"

struct opcode_func_map {
	u16 sub_opcode;
	u16 idev_pkt_size;
	char print_info[16];
	u8 (*idev_handler)(struct ub_bus_controller *ubc, struct vdm_msg_pkt *pkt);
	int idev_pld_size;
};

static void ub_vdm_msg_rsp(struct ub_bus_controller *ubc,
				struct vdm_msg_pkt *pkt, u8 status)
{
	struct msg_pkt_header *header = &pkt->header;
	struct msg_info info = {};
	bool local;
	u32 size;
	int ret;

	size = (MSG_PKT_HEADER_SIZE + VENDOR_GUID_PLD_SIZE +
		VDM_MSG_DW0_PLD_SIZE) << MSG_REQ_SIZE_OFFSET;

	local = ub_rsp_msg_init(header, status,
				VENDOR_GUID_PLD_SIZE + VDM_MSG_DW0_PLD_SIZE);

	message_info_init(&info, local ? ubc->uent : NULL, pkt, NULL, size);
	ret = message_response(ubc->mdev, &info, header->msgetah.code);
	if (ret)
		dev_err(&ubc->dev, "send vdm response message failed, ret=%d\n", ret);
}

struct opcode_func_map idev_func_mapping[] = {};

static int ub_vdm_msg_info_handle(struct ub_bus_controller *ubc,
				    struct vdm_msg_pkt *pkt, u16 len,
				    u16 sub_opcode)
{
	int idx;

	for (idx = 0; idx < ARRAY_SIZE(idev_func_mapping); idx++) {
		if (sub_opcode == idev_func_mapping[idx].sub_opcode) {
			if (len < idev_func_mapping[idx].idev_pkt_size) {
				dev_err(&ubc->dev,
					"Message length[%#x] is wrong\n", len);
				return UB_MSG_RSP_EXEC_EINVAL;
			}
			(void)idev_func_mapping[idx].idev_handler(ubc, pkt);
			return UB_MSG_RSP_SUCCESS;
		}
	}

	dev_err(&ubc->dev, "vdm sub opvode is invalid, sub_opcode = %#x\n", sub_opcode);
	return UB_MSG_RSP_EXEC_EINVAL;
}

static int hi_vdm_vendor_handler(struct ub_bus_controller *ubc, void *pkt, u16 len)
{
	struct vdm_msg_pkt *vdm_pkt = (struct vdm_msg_pkt *)pkt;
	struct msg_pkt_dw0 *pld_dw0 = &vdm_pkt->pld_dw0;
	u16 sub_opcode;
	u8 opcode;

	if (len < (MSG_PKT_HEADER_SIZE + VENDOR_GUID_PLD_SIZE +
		   VDM_MSG_DW0_PLD_SIZE)) {
		dev_err(&ubc->dev, "vdm msg len[%#x] is wrong\n", len);
		return UB_MSG_RSP_EXEC_EINVAL;
	}

	opcode = pld_dw0->opcode;
	sub_opcode = pld_dw0->sub_opcode;
	switch (opcode) {
	case VDM_OPCODE_FM2UB_COMM_MSG:
		return ub_vdm_msg_info_handle(ubc, vdm_pkt, len, sub_opcode);
	default:
		dev_err(&ubc->dev, "vdm opcode type [%u] not support\n",
			opcode);
	}

	return UB_MSG_RSP_EXEC_EINVAL;
}

void hi_vdm_rx_msg_handler(struct ub_bus_controller *ubc, void *pkt, u16 len)
{
	struct msg_pkt_header *header = (struct msg_pkt_header *)pkt;
	u8 sub_msg_code = header->msgetah.sub_msg_code;
	u8 status;

	switch (sub_msg_code) {
	case UB_VENDOR_MSG:
		status = hi_vdm_vendor_handler(ubc, pkt, len);
		break;
	default:
		dev_err(&ubc->dev, "vdm sub msg code[%#x] not support\n",
			sub_msg_code);
		status = UB_MSG_RSP_EXEC_EINVAL;
	}

	if (status)
		ub_vdm_msg_rsp(ubc, (struct vdm_msg_pkt *)pkt, status);
}

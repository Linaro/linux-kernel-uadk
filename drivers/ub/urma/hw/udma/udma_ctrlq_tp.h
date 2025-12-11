/* SPDX-License-Identifier: GPL-2.0+ */
/* Copyright(c) 2025 HiSilicon Technologies CO., Ltd. All rights reserved. */

#ifndef __UDMA_CTRLQ_TP_H__
#define __UDMA_CTRLQ_TP_H__

#include "udma_common.h"

#define UDMA_EID_SIZE		16
#define UDMA_CNA_SIZE		16
#define UDMA_UE_NUM		64

enum udma_ctrlq_cmd_code_type {
	UDMA_CMD_CTRLQ_REMOVE_SINGLE_TP = 0x13,
	UDMA_CMD_CTRLQ_TP_FLUSH_DONE,
	UDMA_CMD_CTRLQ_CHECK_TP_ACTIVE,
	UDMA_CMD_CTRLQ_GET_TP_LIST = 0x21,
	UDMA_CMD_CTRLQ_ACTIVE_TP,
	UDMA_CMD_CTRLQ_DEACTIVE_TP,
	UDMA_CMD_CTRLQ_SET_TP_ATTR,
	UDMA_CMD_CTRLQ_GET_TP_ATTR,
	UDMA_CMD_CTRLQ_MAX
};

enum udma_ctrlq_trans_type {
	UDMA_CTRLQ_TRANS_TYPE_TP_RM = 0,
	UDMA_CTRLQ_TRANS_TYPE_CTP,
	UDMA_CTRLQ_TRANS_TYPE_TP_UM,
	UDMA_CTRLQ_TRANS_TYPE_TP_RC = 4,
	UDMA_CTRLQ_TRANS_TYPE_MAX
};

enum udma_ctrlq_tpid_status {
	UDMA_CTRLQ_TPID_IN_USE = 0,
	UDMA_CTRLQ_TPID_EXITED,
	UDMA_CTRLQ_TPID_IDLE,
};

struct udma_ctrlq_tp_flush_done_req_data {
	uint32_t tpn : 24;
	uint32_t rsv : 8;
};

struct udma_ctrlq_remove_single_tp_req_data {
	uint32_t tpn : 24;
	uint32_t tp_status : 8;
};

struct udma_ctrlq_tpn_data {
	uint32_t tpg_flag : 8;
	uint32_t rsv : 24;
	uint32_t tpgn : 24;
	uint32_t rsv1 : 8;
	uint32_t tpn_cnt : 8;
	uint32_t start_tpn : 24;
};

struct udma_ctrlq_check_tp_active_req_data {
	uint32_t tp_id : 24;
	uint32_t rsv : 8;
	uint32_t pid_flag : 24;
	uint32_t rsv1 : 8;
};

struct udma_ctrlq_check_tp_active_req_info {
	uint32_t num : 8;
	uint32_t rsv : 24;
	struct udma_ctrlq_check_tp_active_req_data data[];
};

struct udma_ctrlq_check_tp_active_rsp_data {
	uint32_t tp_id : 24;
	uint32_t result : 8;
};

struct udma_ctrlq_check_tp_active_rsp_info {
	uint32_t num : 8;
	uint32_t rsv : 24;
	struct udma_ctrlq_check_tp_active_rsp_data data[];
};

enum udma_cmd_ue_opcode {
	UDMA_CMD_UBCORE_COMMAND = 0x1,
	UDMA_CMD_NOTIFY_MUE_SAVE_TP = 0x2,
	UDMA_CMD_NOTIFY_UE_FLUSH_DONE = 0x3,
};

struct udma_ue_tp_info {
	uint32_t tp_cnt : 8;
	uint32_t start_tpn : 24;
};

struct udma_ue_idx_table {
	uint32_t num;
	uint8_t ue_idx[UDMA_UE_NUM];
};

struct udma_notify_flush_done {
	uint32_t tpn;
};

int udma_ctrlq_tp_flush_done(struct udma_dev *udev, uint32_t tpn);
int udma_ctrlq_remove_single_tp(struct udma_dev *udev, uint32_t tpn, int status);
int send_resp_to_ue(struct udma_dev *udma_dev, struct ubcore_resp *req_host,
		    uint8_t dst_ue_idx, uint16_t opcode);

#endif /* __UDMA_CTRLQ_TP_H__ */

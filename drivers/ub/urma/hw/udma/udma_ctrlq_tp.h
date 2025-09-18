/* SPDX-License-Identifier: GPL-2.0+ */
/* Copyright(c) 2025 HiSilicon Technologies CO., Ltd. All rights reserved. */

#ifndef __UDMA_CTRLQ_TP_H__
#define __UDMA_CTRLQ_TP_H__

#include "udma_common.h"

#define UDMA_UE_NUM		64

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

#endif /* __UDMA_CTRLQ_TP_H__ */

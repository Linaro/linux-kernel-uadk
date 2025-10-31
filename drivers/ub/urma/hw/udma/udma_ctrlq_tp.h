/* SPDX-License-Identifier: GPL-2.0+ */
/* Copyright(c) 2025 HiSilicon Technologies CO., Ltd. All rights reserved. */

#ifndef __UDMA_CTRLQ_TP_H__
#define __UDMA_CTRLQ_TP_H__

#include "udma_common.h"

#define UDMA_UE_NUM		64

struct udma_ue_idx_table {
	uint32_t num;
	uint8_t ue_idx[UDMA_UE_NUM];
};

#endif /* __UDMA_CTRLQ_TP_H__ */

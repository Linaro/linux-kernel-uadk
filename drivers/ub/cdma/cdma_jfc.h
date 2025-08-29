/* SPDX-License-Identifier: GPL-2.0+ */
/* Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved. */

#ifndef __CDMA_JFC_H__
#define __CDMA_JFC_H__

#include "cdma_types.h"
#include "cdma_db.h"

enum cdma_jfc_state {
	CDMA_JFC_STATE_INVALID,
	CDMA_JFC_STATE_VALID,
	CDMA_JFC_STATE_ERROR
};

struct cdma_jfc {
	struct cdma_base_jfc base;
	u32 jfcn;
	u32 ceqn;
	u32 tid;
	struct cdma_buf buf;
	struct cdma_sw_db db;
	u32 ci;
	u32 arm_sn;
	spinlock_t lock;
	u32 mode;
};

struct cdma_jfc_ctx {
	/* DW0 */
	u32 state : 2;
	u32 arm_st : 2;
	u32 shift : 4;
	u32 cqe_size : 1;
	u32 record_db_en : 1;
	u32 jfc_type : 1;
	u32 inline_en : 1;
	u32 cqe_va_l : 20;
	/* DW1 */
	u32 cqe_va_h;
	/* DW2 */
	u32 cqe_token_id : 20;
	u32 cq_cnt_mode : 1;
	u32 rsv0 : 3;
	u32 ceqn : 8;
	/* DW3 */
	u32 cqe_token_value : 24;
	u32 rsv1 : 8;
	/* DW4 */
	u32 pi : 22;
	u32 cqe_coalesce_cnt : 10;
	/* DW5 */
	u32 ci : 22;
	u32 cqe_coalesce_period : 3;
	u32 rsv2 : 7;
	/* DW6 */
	u32 record_db_addr_l;
	/* DW7 */
	u32 record_db_addr_h : 26;
	u32 rsv3 : 6;
	/* DW8 */
	u32 push_usi_en : 1;
	u32 push_cqe_en : 1;
	u32 token_en : 1;
	u32 rsv4 : 9;
	u32 tpn : 20;
	/* DW9 ~ DW12 */
	u32 rmt_eid[4];
	/* DW13 */
	u32 seid_idx : 10;
	u32 rmt_token_id : 20;
	u32 rsv5 : 2;
	/* DW14 */
	u32 remote_token_value;
	/* DW15 */
	u32 int_vector : 16;
	u32 stars_en : 1;
	u32 rsv6 : 15;
	/* DW16 */
	u32 poll : 1;
	u32 cqe_report_timer : 24;
	u32 se : 1;
	u32 arm_sn : 2;
	u32 rsv7 : 4;
	/* DW17 */
	u32 se_cqe_idx : 24;
	u32 rsv8 : 8;
	/* DW18 */
	u32 wr_cqe_idx : 22;
	u32 rsv9 : 10;
	/* DW19 */
	u32 cqe_cnt : 24;
	u32 rsv10 : 8;
	/* DW20 ~ DW31 */
	u32 rsv11[12];
};

int cdma_post_destroy_jfc_mbox(struct cdma_dev *cdev, u32 jfcn,
			       enum cdma_jfc_state state);

int cdma_delete_jfc(struct cdma_dev *cdev, u32 jfcn,
		    struct cdma_cmd_delete_jfc_args *arg);

#endif /* CDMA_JFC_H */

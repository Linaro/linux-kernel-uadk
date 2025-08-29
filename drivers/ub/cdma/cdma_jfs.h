/* SPDX-License-Identifier: GPL-2.0+ */
/* Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved. */

#ifndef __CDMA_JFS_H__
#define __CDMA_JFS_H__

#include "cdma_common.h"
#include "cdma_types.h"

#define MAX_WQEBB_NUM 4
#define CDMA_JFS_WQEBB_SIZE 64
#define CDMA_JFS_SGE_SIZE 16
#define SQE_WRITE_NOTIFY_CTL_LEN 80

#define CDMA_TA_TIMEOUT_128MS 128
#define CDMA_TA_TIMEOUT_1000MS 1000
#define CDMA_TA_TIMEOUT_8000MS 8000
#define CDMA_TA_TIMEOUT_64000MS 64000

#define CDMA_RCV_SEND_MAX_DIFF 512U

struct cdma_jfs {
	struct cdma_base_jfs base_jfs;
	struct cdma_dev *dev;
	struct cdma_jetty_queue sq;
	struct cdma_jfs_cfg cfg;
	u64 jfs_addr;
	u32 id;
	u32 queue_id;
	bool is_kernel;
	refcount_t ae_ref_cnt;
	struct completion ae_comp;
};

struct cdma_jfs_ctx {
	/* DW0 */
	u32 ta_timeout : 2;
	u32 rnr_retry_num : 3;
	u32 type : 3;
	u32 sqe_bb_shift : 4;
	u32 sl : 4;
	u32 state : 3;
	u32 jfs_mode : 1;
	u32 sqe_token_id_l : 12;
	/* DW1 */
	u32 sqe_token_id_h : 8;
	u32 err_mode : 1;
	u32 rsv0 : 1;
	u32 cmp_odr : 1;
	u32 rsv1 : 1;
	u32 sqe_base_addr_l : 20;
	/* DW2 */
	u32 sqe_base_addr_h;
	/* DW3 */
	u32 rsv2;
	/* DW4 */
	u32 tx_jfcn : 20;
	u32 jfrn_l : 12;
	/* DW5 */
	u32 jfrn_h : 8;
	u32 rsv3 : 4;
	u32 rx_jfcn : 20;
	/* DW6 */
	u32 seid_idx : 10;
	u32 rsv4 : 22;
	/* DW7 */
	u32 user_data_l;
	/* DW8 */
	u32 user_data_h;
	/* DW9 */
	u32 sqe_pos : 1;
	u32 sqe_pld_pos : 1;
	u32 sqe_pld_tokenid : 20;
	u32 rsv5 : 10;
	/* DW10 */
	u32 tpn : 24;
	u32 rsv6 : 8;
	/* DW11 */
	u32 rmt_eid : 20;
	u32 rsv7 : 12;
	/* DW12 */
	u32 rmt_tokenid : 20;
	u64 rsv9 : 12;
	/* DW13-DW15 */
	u32 rsv12[3];
	/* DW16 */
	u32 next_send_ssn : 16;
	u32 src_order_wqe : 16;
	/* DW17 */
	u32 src_order_ssn : 16;
	u32 src_order_sgme_cnt : 16;
	/* DW18 */
	u32 src_order_sgme_send_cnt : 16;
	u32 ci : 16;
	/* DW19 */
	u32 rsv13;
	/* DW20 */
	u32 pi : 16;
	u32 sq_db_doing : 1;
	u32 ost_rce_credit : 15;
	/* DW21 */
	u32 sq_db_retrying : 1;
	u32 wmtp_rsv0 : 31;
	/* DW22 */
	u32 wait_ack_timeout : 1;
	u32 wait_rnr_timeout : 1;
	u32 cqe_ie : 1;
	u32 cqe_sz : 1;
	u32 wmtp_rsv1 : 28;
	/* DW23 */
	u32 wml_rsv1;
	/* DW24 */
	u32 next_rcv_ssn : 16;
	u32 next_cpl_bb_idx : 16;
	/* DW25 */
	u32 next_cpl_sgmt_num : 20;
	u32 we_rsv0 : 12;
	/* DW26 */
	u32 next_cpl_bb_num : 4;
	u32 next_cpl_cqe_en : 1;
	u32 next_cpl_info_vld : 1;
	u32 rpting_cqe : 1;
	u32 not_rpt_cqe : 1;
	u32 flush_ssn : 16;
	u32 flush_ssn_vld : 1;
	u32 flush_vld : 1;
	u32 flush_cqe_done : 1;
	u32 we_rsv1 : 5;
	/* DW27 */
	u32 rcved_cont_ssn_num : 20;
	u32 we_rsv2 : 12;
	/* DW28 */
	u32 sq_timer;
	/* DW29 */
	u32 rnr_cnt : 3;
	u32 abt_ssn : 16;
	u32 abt_ssn_vld : 1;
	u32 taack_timeout_flag : 1;
	u32 we_rsv3 : 9;
	u32 err_type_l : 2;
	/* DW30 */
	u32 err_type_h : 7;
	u32 sq_flush_ssn : 16;
	u32 we_rsv4 : 9;
	/* DW31 */
	u32 avail_sgmt_ost : 10;
	u32 read_op_cnt : 10;
	u32 we_rsv5 : 12;
	/* DW32 - DW63 */
	u32 taack_nack_bm[32];
};

struct cdma_base_jfs *cdma_create_jfs(struct cdma_dev *cdev,
				      struct cdma_jfs_cfg *cfg,
				      struct cdma_udata *udata);
int cdma_delete_jfs(struct cdma_dev *cdev, u32 jfs_id);

#endif

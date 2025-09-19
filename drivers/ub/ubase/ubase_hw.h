/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved.
 *
 */

#ifndef __UBASE_HW_H__
#define __UBASE_HW_H__

#include <ub/ubase/ubase_comm_hw.h>

#include "ubase_cmd.h"

#define UBASE_CTX_REMOVE_ALL		(-2)

#define UBASE_DEF_CEQ_VECTOR_NUM	1
#define UBASE_DEF_AEQ_VECTOR_NUM	1
#define UBASE_DEF_MISC_VERCTOR_NUM	1
#define UBASE_DEF_PUBLIC_JETTY_CNT	1024
#define UBASE_DEF_EQE_SIZE		64
#define UBASE_DEF_AEQ_DEPTH		512
#define UBASE_DEF_CEQ_DEPTH		4096

struct ubase_caps_item {
	void		*p;
	u32		default_val;
	u8		size;
	const char	*name;
};

struct ubase_res_cmd_resp {
	__le32	cap_bits[UBASE_CAP_LEN];
	__le32	rsvd[3];

	u8	rsvd1[2];
	__le16	ceq_vector_num;
	__le16	aeq_vector_num;
	__le16	misc_vector_num;
	__le16	aeqe_size;
	__le16	ceqe_size;
	__le16	udma_cqe_size;
	__le16	nic_cqe_size;
	__le32	aeqe_depth;
	__le32	ceqe_depth;
	__le32	udma_jfs_max_cnt;
	__le32	udma_jfs_reserved_cnt;

	__le32	udma_jfs_depth;
	__le32	udma_jfr_max_cnt;
	__le32	udma_jfr_reserved_cnt;
	__le32	udma_jfr_depth;
	u8	nic_vl_num;
	u8	rsvd2[3];
	u8	nic_vl[UBASE_MAX_REQ_VL_NUM];
	__le32	udma_jfc_max_cnt;

	__le32	udma_jfc_reserved_cnt;
	__le32	udma_jfc_depth;
	__le32	udma_tp_max_cnt;
	__le32	udma_tp_reserved_cnt;
	__le32	udma_tp_depth;
	__le32	udma_tpg_max_cnt;
	__le32	udma_tpg_reserved_cnt;
	__le32	udma_tpg_depth;

	__le32	nic_jfs_max_cnt;
	__le32	nic_jfs_reserved_cnt;
	__le32	nic_jfs_depth;
	__le32	nic_jfr_max_cnt;
	__le32	nic_jfr_reserved_cnt;
	__le32	nic_jfr_depth;
	__le32	rsvd3[2];

	__le32	rsvd4;
	__le32	nic_jfc_max_cnt;
	__le32	nic_jfc_reserved_cnt;
	__le32	nic_jfc_depth;
	__le32	nic_tp_max_cnt;
	__le32	nic_tp_reserved_cnt;
	__le32	nic_tp_depth;
	__le32	nic_tpg_max_cnt;

	__le32	nic_tpg_reserved_cnt;
	__le32	nic_tpg_depth;
	__le32	total_ue_num;
	__le32	jfs_ctx_size;
	__le32	jfr_ctx_size;
	__le32	jfc_ctx_size;
	__le32	tp_ctx_size;
	__le16	rsvd_jetty_cnt;
	__le16	mac_stats_num;

	__le32	ta_extdb_buf_size;
	__le32	ta_timer_buf_size;
	__le32	public_jetty_cnt;
	__le32	tp_extdb_buf_size;
	__le32	tp_timer_buf_size;
	u8	port_work_mode;
	u8	udma_vl_num;
	u8	udma_tp_resp_vl_offset;
	u8	ue_num;
	__le32	port_bitmap;
	u8	rsvd5[4];

	/* include udma tp and ctp req vl */
	u8	udma_req_vl[UBASE_MAX_REQ_VL_NUM];
	__le32	udma_rc_depth;
	u8	rsvd6[4];
	__le32	jtg_max_cnt;
	__le32	rc_max_cnt_per_vl;
	__le32	dest_addr_max_cnt;
	__le32	seid_upi_max_cnt;

	__le32	tpm_max_cnt;
	__le32	ccc_max_cnt;
};

struct ubase_query_oor_resp {
	u8	oor_en;
	u8	reorder_cq_buffer_en;
	u8	reorder_cap;
	u8	reorder_cq_shift;
	__le32	on_flight_size;
	u8	dynamic_ack_timeout;
	u8	rsvd0[15];
};

struct ubase_query_controller_info_resp {
	__le32	rsvd0[2];
	u8	packet_pattern_mode : 1;
	u8	ack_queue_num : 4;
	u8	rsvd1 : 3;
	u8	rsvd2[15];
};

struct ubase_cfg_dma_buf_req {
	__le32 addr_l;
	__le32 addr_h;
	__le32 tp_num; /* only used when cfg TP extdb buf */
	__le32 resv[3];
};

struct ubase_config_sl_vl_cmd {
	u8	sl_num;
	u8	sl_vl[23];
};

struct ubase_query_chip_die_cmd {
	__le16	nl_port_id;
	__le16	chip_id;
	__le16	die_id;
	__le16	io_port_id;
	__le16	ue_id;
	__le16	ub_port_logic_id;
	__le16	nl_id;
	__le16	io_port_logic_id;
};

struct ubase_ctx_buf_map {
	struct ubase_ctx_buf_cap *ctx;
	u16 mb_cmd;
};

struct ubase_query_vl_ageing_cmd {
	__le16	vl_ageing_en;
	u8	rsv[22];
};

struct ubase_query_ctp_vl_offset_cmd {
	u8	ctp_vl_offset;
	u8	rsv[23];
};

int ubase_hw_init(struct ubase_dev *udev);
void ubase_hw_uninit(struct ubase_dev *udev);
int ubase_query_sl_vl_map(struct ubase_dev *udev, u8 *sl_vl);
int ubase_qos_init(struct ubase_dev *udev);
int ubase_query_ets_tc(struct ubase_dev *udev, u32 port_bitmap,
		       u16 vl_bitmap, struct ubase_cfg_ets_vl_sch_cmd *resp);
int ubase_query_ets_tcg(struct ubase_dev *udev,
			struct ubase_query_ets_tcg_cmd *resp);
int ubase_query_ets_port(struct ubase_dev *udev,
			 struct ubase_query_ets_port_cmd *resp);
int ubase_query_dev_res(struct ubase_dev *udev);
int ubase_query_chip_info(struct ubase_dev *udev);
int ubase_query_controller_info(struct ubase_dev *udev);
int ubase_query_hw_oor_caps(struct ubase_dev *udev);
int ubase_ue_init(struct ubase_dev *udev);
void ubase_ue_uninit(struct ubase_dev *udev);
int ubase_query_fst_fvt_rqmt(struct ubase_dev *udev,
			     struct ubase_query_fst_fvt_rqmt_cmd *resp,
			     u16 bus_ue_id);

#endif

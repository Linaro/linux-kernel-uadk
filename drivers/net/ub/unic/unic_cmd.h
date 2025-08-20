/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved.
 *
 */

#ifndef __UNIC_CMD_H__
#define __UNIC_CMD_H__

#include <linux/kernel.h>
#include <ub/ubase/ubase_comm_cmd.h>
#include <ub/ubase/ubase_comm_dev.h>

#include "unic.h"

struct unic_res_cmd_resp {
	__le32	cap_bits[UNIC_CAP_LEN];
	__le32	rsvd[3];

	u8	media_type;
	u8	default_lanes;
	__le16	rx_buff_len;
	__le32	speed_ability;
	__le32	default_speed;
	__le16	total_ip_tbl_size;
	u8	rsvd1[2];
	__le32	guid_tbl_size;
	u8	rsvd2[4];
	__le32	uc_mac_tbl_size;
	__le32	vlan_tbl_size;

	__le32	mng_tbl_size;
	__le16	max_trans_unit;
	__le16	min_trans_unit;
	u8	rsvd3[4];
	__le16	vport_buf_size; /* unit: KB */
	u8	vport_buf_num;
	u8	rsvd4[5];
	__le16	max_int_ql;
	__le16	max_int_gl;
	__le32	mc_mac_tbl_size;
	u8	rsvd5[4];
};

struct unic_config_max_trans_unit_cmd {
	__le16  max_trans_unit;
	u8      rsv[22];
};

struct unic_ld_config_mode_cmd {
	__le32 txrx_en;
	u8 rsv[20];
};

struct unic_link_status_cmd_resp {
	u8	status;
	u8	link_fail_code;
	u8	rsvd[22];
};

struct unic_query_port_info_resp {
	__le32	speed;
	__le32	speed_ability;
	__le16	fec_mode;
	__le16	fec_ability;
	u8	autoneg;
	u8	autoneg_ability;
	u8	lanes;
	u8	module_type;
	u8	rsv[8];
};

struct unic_cfg_speed_dup_cmd {
	__le32	speed;
	u8	duplex;
	u8	lanes;
	u8	rsv[18];
};

struct unic_cfg_autoneg_mode_cmd {
	u8	autoneg_en;
	u8	rsv[23];
};

struct unic_promisc_cfg_cmd {
	u8 rsv0;
	u8 promisc_uc_ind : 1; /* indicate if uc enable is set or not */
	u8 promisc_mc_ind : 1; /* indicate if mc enable is set or not */
	u8 rsv1 : 6;
	u8 promisc_rx_uc_ip_en : 1; /* promisc rx ip uc enable or not */
	u8 promisc_rx_uc_guid_en : 1; /* promisc rx guid uc enable or not */
	u8 promisc_rx_mc_en : 1; /* promisc rx mc enable or not */
	u8 promisc_rx_uc_mac_en : 1; /* promisc rx mac uc enable or not */
	u8 promisc_rx_mc_mac_en : 1; /* promisc rx mac mc enable or not */
	u8 promisc_rx_bc_en : 1; /* promisc rx bc enable or not */
	u8 rsv2 : 2;
	u8 rsv3[21];
};

struct unic_query_net_guid_cmd {
	u8	guid[16];
	u8	rsvd[8];
};

struct unic_cfg_vport_buf_cmd {
	u8	buf_num;
	u8	rsvd[7];
#define U32S_PER_U64 2
	__le32	buf_addr[UNIC_MAX_VPORT_BUF_NUM * U32S_PER_U64];
};

struct unic_cfg_fec_cmd {
	__le32	fec_mode;
	u8	rsv[20];
};

struct unic_query_fec_stats_item {
	__le32 corr_blocks_l;
	__le32 corr_blocks_h;
	__le32 uncorr_blocks_l;
	__le32 uncorr_blocks_h;
	__le32 corr_bits_l;
	__le32 corr_bits_h;
};

#define UNIC_FEC_STATS_MAX_LANE	8
struct unic_query_fec_stats_resp {
	struct unic_query_fec_stats_item total;
	struct unic_query_fec_stats_item lane[UNIC_FEC_STATS_MAX_LANE];
	u8 lane_num;
	u8 rsv[31];
};

struct unic_query_flush_status_resp {
	u8 status;
	u8 rsv[23];
};

enum unic_vl_map_type {
	UNIC_PRIO_VL_MAP,
	UNIC_DSCP_VL_MAP,
};

struct unic_config_vl_map_cmd {
	u8 map_type;
	u8 resv0[3];
	u8 prio_vl[UNIC_MAX_PRIO_NUM];
	u8 dscp_vl[UBASE_MAX_DSCP];
	u8 resv1[12];
};

struct unic_config_vl_speed_cmd {
	__le16 bus_ue_id;
	__le16 vl_bitmap;
	__le32 max_speed[UBASE_MAX_VL_NUM];
	u8 resv1[20];
};

#endif

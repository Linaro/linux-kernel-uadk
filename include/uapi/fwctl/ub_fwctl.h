/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note*/
/*
 * Copyright(c) 2025 HiSilicon Technologies CO., Limited. All rights reserved.
 */

#ifndef _UAPI_UB_FWCTL_H_
#define _UAPI_UB_FWCTL_H_

#include <linux/types.h>

/**
 * struct fwctl_rpc_ub_in - ioctl(FWCTL_RPC) input
 * @rpc_cmd: user specified opcode
 * @data_size: Length of @data
 * @version: Version passed in by the user
 * @rsvd: reserved
 * @data: user inputs specified input data
 */
struct fwctl_rpc_ub_in {
	__u32 rpc_cmd;
	__u32 data_size;
	__u32 version;
	__u32 rsvd;
	__u32 data[] __counted_by(data_size);
};

/**
 * struct fwctl_rpc_ub_out - ioctl(FWCTL_RPC) output
 * @retval: The value returned when querying data with an error message
 * @data_size: Length of @data
 * @data: data transmitted to users
 */
struct fwctl_rpc_ub_out {
	int retval;
	__u32 data_size;
	__u32 data[];
};

enum ub_fwctl_cmdrpc_type {
	UTOOL_CMD_QUERY_NL = 0x0001,
	UTOOL_CMD_QUERY_NL_PKT_STATS = 0x0002,
	UTOOL_CMD_QUERY_NL_SSU_STATS = 0x0003,
	UTOOL_CMD_QUERY_NL_ABN = 0x0004,

	UTOOL_CMD_QUERY_TP = 0x0021,
	UTOOL_CMD_QUERY_TP_PKT_STATS = 0x0022,
	UTOOL_CMD_QUERY_TP_TX_ROUTE = 0x0023,
	UTOOL_CMD_QUERY_TP_ABN_STATS = 0x0024,
	UTOOL_CMD_QUERY_TP_RX_BANK = 0x0025,

	UTOOL_CMD_QUERY_DL = 0x0011,
	UTOOL_CMD_QUERY_DL_PKT_STATS = 0x0012,
	UTOOL_CMD_QUERY_DL_LINK_STATUS = 0x0013,
	UTOOL_CMD_QUERY_DL_LANE = 0x0014,
	UTOOL_CMD_QUERY_DL_BIT_ERR = 0x0015,
	UTOOL_CMD_QUERY_DL_LINK_TRACE = 0x0016,
	UTOOL_CMD_QUERY_DL_BIST = 0x0017,
	UTOOL_CMD_CONF_DL_BIST = 0x0018,
	UTOOL_CMD_QUERY_DL_BIST_ERR = 0x0019,

	UTOOL_CMD_QUERY_TA = 0x0031,
	UTOOL_CMD_QUERY_TA_PKT_STATS = 0x0032,
	UTOOL_CMD_QUERY_TA_ABN_STATS = 0x0033,

	UTOOL_CMD_QUERY_BA = 0x0041,
	UTOOL_CMD_QUERY_BA_PKT_STATS = 0x0042,
	UTOOL_CMD_QUERY_BA_MAR = 0x0043,
	UTOOL_CMD_QUERY_BA_MAR_TABLE = 0x0044,
	UTOOL_CMD_QUERY_BA_MAR_CYC_EN = 0x0045,
	UTOOL_CMD_CONF_BA_MAR_CYC_EN = 0x0046,
	UTOOL_CMD_CONFIG_BA_MAR_PEFR_STATS = 0x0047,
	UTOOL_CMD_QUERY_BA_MAR_PEFR_STATS = 0x0048,

	UTOOL_CMD_QUERY_QOS = 0x0051,

	UTOOL_CMD_QUERY_SCC_VERSION = 0x0061,
	UTOOL_CMD_QUERY_SCC_LOG = 0x0062,
	UTOOL_CMD_QUERY_SCC_DEBUG_EN = 0x0063,
	UTOOL_CMD_CONF_SCC_DEBUG_EN = 0x0064,

	UTOOL_CMD_QUERY_QUEUE = 0x0073,

	UTOOL_CMD_QUERY_PORT_INFO = 0x0081,
	UTOOL_CMD_QUERY_IO_DIE_PORT_INFO = 0x0082,

	UTOOL_CMD_QUERY_UBOMMU = 0x0091,

	UTOOL_CMD_QUERY_ECC_2B = 0x00B1,

	UTOOL_CMD_QUERY_LOOPBACK = 0x00D1,
	UTOOL_CMD_CONF_LOOPBACK = 0x00D2,
	UTOOL_CMD_QUERY_PRBS_EN = 0x00D3,
	UTOOL_CMD_CONF_PRBS_EN = 0x00D4,
	UTOOL_CMD_QUERY_PRBS_RESULT = 0x00D5,

	UTOOL_CMD_QUERY_DUMP = 0xFFFE,

	UTOOL_CMD_QUERY_MAX,
};

struct fwctl_pkt_in_enable {
	__u8 enable;
};

struct fwctl_pkt_in_table {
	__u32 port_id;
	__u32 table_num;
	__u32 index;
};

struct fwctl_pkt_in_port {
	__u32 port_id;
};

struct fwctl_pkt_in_index {
	__u32 index;
};

#endif

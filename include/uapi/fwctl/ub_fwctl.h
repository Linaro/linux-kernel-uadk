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

	UTOOL_CMD_QUERY_DL = 0x0011,
	UTOOL_CMD_QUERY_DL_PKT_STATS = 0x0012,
	UTOOL_CMD_QUERY_DL_LINK_STATUS = 0x0013,
	UTOOL_CMD_QUERY_DL_LANE = 0x0014,
	UTOOL_CMD_QUERY_DL_BIT_ERR = 0x0015,
	UTOOL_CMD_QUERY_DL_BIST = 0x0017,
	UTOOL_CMD_CONF_DL_BIST = 0x0018,
	UTOOL_CMD_QUERY_DL_BIST_ERR = 0x0019,

	UTOOL_CMD_QUERY_TA = 0x0031,
	UTOOL_CMD_QUERY_TA_PKT_STATS = 0x0032,
	UTOOL_CMD_QUERY_TA_ABN_STATS = 0x0033,

	UTOOL_CMD_QUERY_MAX,
};

#endif

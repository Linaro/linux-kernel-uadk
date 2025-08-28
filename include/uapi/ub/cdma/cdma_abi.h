/* SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note */
/* Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved. */

#ifndef _UAPI_UB_CDMA_CDMA_ABI_H_
#define _UAPI_UB_CDMA_CDMA_ABI_H_

#include <linux/types.h>

/* cdma ioctl cmd */
#define CDMA_IOC_MAGIC 'C'
#define CDMA_SYNC _IOWR(CDMA_IOC_MAGIC, 0, struct cdma_ioctl_hdr)

enum cdma_cmd {
	CDMA_CMD_QUERY_DEV_INFO,
	CDMA_CMD_CREATE_CTX,
	CDMA_CMD_CREATE_QUEUE,
	CDMA_CMD_MAX
};

struct cdma_ioctl_hdr {
	__u32 command;
	__u32 args_len;
	__u64 args_addr;
};

struct dev_eid {
	__u32 dw0;
	__u32 dw1;
	__u32 dw2;
	__u32 dw3;
};

struct eu_info {
	__u32 eid_idx;
	struct dev_eid eid;
	__u32 upi;
};

struct cdma_device_cap {
	__u32 max_jfc;
	__u32 max_jfs;
	__u32 max_jfc_depth;
	__u32 max_jfs_depth;
	__u32 max_jfs_inline_len;
	__u32 max_jfs_sge;
	__u32 max_jfs_rsge;
	__u64 max_msg_size;
	__u32 max_atomic_size;
	__u16 trans_mode;
	__u32 ceq_cnt;
	__u32 max_eid_cnt;
	__u64 page_size_cap;
};

struct cdma_device_attr {
#define CDMA_MAX_EU_NUM 64
	__u8 eu_num;
	struct dev_eid eid;
	struct eu_info eu;
	struct eu_info eus[CDMA_MAX_EU_NUM];
	struct cdma_device_cap dev_cap;
};

struct cdma_cmd_query_device_attr_args {
	struct {
		struct cdma_device_attr attr;
	} out;
};

struct cdma_create_context_args {
	struct {
		__u8 cqe_size;
		__u8 dwqe_enable;
		int async_fd;
	} out;
};

struct cdma_cmd_create_queue_args {
	struct {
		__u32 queue_depth;
		__u32 dcna;
		__u32 rmt_eid;
		__u8  priority;
		__u64 user_ctx;
		__u32 trans_mode;
	} in;
	struct {
		int queue_id;
		__u64 handle;
	} out;
};

#endif

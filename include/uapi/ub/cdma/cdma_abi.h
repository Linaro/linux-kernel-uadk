/* SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note */
/* Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved. */

#ifndef _UAPI_UB_CDMA_CDMA_ABI_H_
#define _UAPI_UB_CDMA_CDMA_ABI_H_

#include <linux/types.h>

/* cdma ioctl cmd */
#define CDMA_IOC_MAGIC 'C'
#define CDMA_SYNC _IOWR(CDMA_IOC_MAGIC, 0, struct cdma_ioctl_hdr)

/* cdma event ioctl cmd */
#define CDMA_EVENT_CMD_MAGIC 'F'
#define JFAE_CMD_GET_ASYNC_EVENT 0

#define CDMA_CMD_GET_ASYNC_EVENT	\
	_IOWR(CDMA_EVENT_CMD_MAGIC, JFAE_CMD_GET_ASYNC_EVENT, struct cdma_cmd_async_event)

#define CDMA_DOORBELL_OFFSET 0x80

#define MAP_COMMAND_MASK 0xff
#define MAP_INDEX_MASK 0xffffff
#define MAP_INDEX_SHIFT 8

/* cdma queue cfg deault value */
#define CDMA_TYPICAL_RNR_RETRY 7
#define CDMA_TYPICAL_ERR_TIMEOUT 2 /* 0:128ms 1:1s 2:8s 3:64s */

enum db_mmap_type {
	CDMA_MMAP_JFC_PAGE,
	CDMA_MMAP_JETTY_DSQE
};

enum cdma_cmd {
	CDMA_CMD_QUERY_DEV_INFO,
	CDMA_CMD_CREATE_CTX,
	CDMA_CMD_DELETE_CTX,
	CDMA_CMD_CREATE_CTP,
	CDMA_CMD_DELETE_CTP,
	CDMA_CMD_CREATE_JFS,
	CDMA_CMD_DELETE_JFS,
	CDMA_CMD_CREATE_QUEUE,
	CDMA_CMD_DELETE_QUEUE,
	CDMA_CMD_CREATE_JFC,
	CDMA_CMD_DELETE_JFC,
	CDMA_CMD_MAX
};

struct cdma_ioctl_hdr {
	__u32 command;
	__u32 args_len;
	__u64 args_addr;
};

struct cdma_create_jfs_ucmd {
	__u64 buf_addr;
	__u32 buf_len;
	__u64 db_addr;
	__u64 idx_addr;
	__u32 idx_len;
	__u64 jetty_addr;
	__u32 sqe_bb_cnt;
	__u32 jetty_type;
	__u32 non_pin;
	__u32 jfs_id;
	__u32 queue_id;
	__u32 tid;
};

struct cdma_cmd_udrv_priv {
	__u64 in_addr;
	__u32 in_len;
	__u64 out_addr;
	__u32 out_len;
};

struct cdma_cmd_create_jfs_args {
	struct {
		__u32 depth;
		__u32 flag;
		__u32 eid_idx;
		__u8 priority;
		__u8 max_sge;
		__u8 max_rsge;
		__u8 retry_cnt;
		__u8 rnr_retry;
		__u8 err_timeout;
		__u32 jfc_id;
		__u32 queue_id;
		__u32 rmt_eid;
		__u32 pld_token_id;
		__u32 tpn;
		__u64 dma_jfs; /* dma jfs pointer */
		__u32 trans_mode;
	} in;
	struct {
		__u32 id;
		__u32 depth;
		__u8 max_sge;
		__u8 max_rsge;
		__u64 handle;
	} out;
	struct cdma_cmd_udrv_priv udata;
};

struct cdma_cmd_async_event {
	__u64 event_data;
	__u32 event_type;
};

struct cdma_cmd_delete_jfs_args {
	struct {
		__u32 jfs_id;
		__u64 handle;
		__u32 queue_id;
	} in;
	struct {
	} out;
};

struct cdma_cmd_create_ctp_args {
	struct {
		__u32 scna;
		__u32 dcna;
		__u32 eid_idx;
		__u32 upi;
		__u64 dma_tp;
		__u32 seid;
		__u32 deid;
		__u32 queue_id;
	} in;
	struct {
		__u32 tpn;
		__u64 handle;
	} out;
};

struct cdma_cmd_delete_ctp_args {
	struct {
		__u32 tpn;
		__u64 handle;
		__u32 queue_id;
	} in;
	struct {
	} out;
};

struct cdma_cmd_create_jfc_args {
	struct {
		__u32 depth; /* in terms of CQEBB */
		int jfce_fd;
		int jfce_id;
		__u32 ceqn;
		__u32 queue_id;
	} in;
	struct {
		__u32 id;
		__u32 depth;
		__u64 handle; /* handle of the allocated jfc obj in kernel */
	} out;
	struct cdma_cmd_udrv_priv udata;
};

struct cdma_cmd_delete_jfc_args {
	struct {
		__u32 jfcn;
		__u64 handle; /* handle of jfc */
		__u32 queue_id;
	} in;
	struct {
		__u32 async_events_reported;
	} out;
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

struct cdma_jfc_db {
	__u32 ci : 24;
	__u32 notify : 1;
	__u32 arm_sn : 2;
	__u32 type : 1;
	__u32 rsv1 : 4;
	__u32 jfcn : 20;
	__u32 rsv2 : 12;
};

struct cdma_create_jfc_ucmd {
	__u64 buf_addr;
	__u32 buf_len;
	__u64 db_addr;
	__u32 mode;
	__u32 tid;
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

struct cdma_cmd_delete_queue_args {
	struct {
		__u32 queue_id;
		__u64 handle;
	} in;
};

#endif

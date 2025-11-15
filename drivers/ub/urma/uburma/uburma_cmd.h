/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2025. All rights reserved.
 *
 * Description: uburma cmd header file
 * Author: Qian Guoxin
 * Create: 2023-2-28
 * Note:
 * History: 2023-2-28: Create file
 */

#ifndef UBURMA_CMD_H
#define UBURMA_CMD_H

#include <uapi/linux/in.h>
#include <uapi/linux/in6.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/socket.h>

#include "ub/urma/ubcore_types.h"
#include "uburma_types.h"

struct uburma_cmd_hdr {
	uint32_t command;
	uint32_t args_len;
	uint64_t args_addr;
};

#define UBURMA_CMD_MAX_ARGS_SIZE 25600
#define UBURMA_CMD_MAX_PORT_CNT 8
#define UBURMA_CMD_TP_ATTR_BYTES 128

/* only for uburma device ioctl */
#define UBURMA_CMD_MAGIC 'U'
#define UBURMA_CMD _IOWR(UBURMA_CMD_MAGIC, 1, struct uburma_cmd_hdr)

enum uburma_cmd {
	UBURMA_CMD_CREATE_CTX = 1,
	UBURMA_CMD_REGISTER_SEG = 4,
	UBURMA_CMD_UNREGISTER_SEG = 5,
	UBURMA_CMD_CREATE_JFS = 8,
	UBURMA_CMD_MODIFY_JFS = 9,
	UBURMA_CMD_QUERY_JFS = 10,
	UBURMA_CMD_DELETE_JFS = 11,
	UBURMA_CMD_CREATE_JFR = 12,
	UBURMA_CMD_MODIFY_JFR = 13,
	UBURMA_CMD_QUERY_JFR = 14,
	UBURMA_CMD_DELETE_JFR = 15,
	UBURMA_CMD_CREATE_JFC = 16,
	UBURMA_CMD_MODIFY_JFC = 17,
	UBURMA_CMD_DELETE_JFC = 18,
	UBURMA_CMD_CREATE_JFCE = 19,
	UBURMA_CMD_CREATE_JETTY = 22,
	UBURMA_CMD_MODIFY_JETTY,
	UBURMA_CMD_QUERY_JETTY,
	UBURMA_CMD_DELETE_JETTY = 25,
	UBURMA_CMD_CREATE_JETTY_GRP = 34,
	UBURMA_CMD_DESTROY_JETTY_GRP = 35,
	UBURMA_CMD_DELETE_JFS_BATCH = 50,
	UBURMA_CMD_DELETE_JFR_BATCH,
	UBURMA_CMD_DELETE_JFC_BATCH,
	UBURMA_CMD_DELETE_JETTY_BATCH,
	UBURMA_CMD_MAX
};

struct uburma_cmd_udrv_priv {
	uint64_t in_addr;
	uint32_t in_len;
	uint64_t out_addr;
	uint32_t out_len;
};

struct uburma_cmd_create_ctx {
	struct {
		uint8_t eid[UBCORE_EID_SIZE];
		uint32_t eid_index;
	} in;
	struct {
		int async_fd;
	} out;
	struct uburma_cmd_udrv_priv udata;
};

struct uburma_cmd_register_seg {
	struct {
		uint64_t va;
		uint64_t len;
		uint32_t token_id;
		uint64_t token_id_handle;
		uint32_t token;
		uint32_t flag;
	} in;
	struct {
		uint32_t token_id;
		uint64_t handle; /* handle of the allocated seg obj in kernel */
	} out;
	struct uburma_cmd_udrv_priv udata;
};

struct uburma_cmd_unregister_seg {
	struct {
		uint64_t handle; /* handle of seg, used to find seg obj in kernel */
	} in;
};

struct uburma_cmd_create_jfr {
	struct {
		uint32_t depth;
		uint32_t flag;
		uint32_t trans_mode;
		uint8_t max_sge;
		uint8_t min_rnr_timer;
		uint32_t jfc_id;
		uint64_t jfc_handle;
		uint32_t token;
		uint32_t id;
		uint64_t urma_jfr; /* urma jfr pointer */
	} in;
	struct {
		uint32_t id;
		uint32_t depth;
		uint8_t max_sge;
		uint64_t handle; /* handle of the allocated jfr obj in kernel */
	} out;
	struct uburma_cmd_udrv_priv udata;
};

struct uburma_cmd_modify_jfr {
	struct {
		uint64_t handle; /* handle of jfr, used to find jfr obj in kernel */
		uint32_t mask; /* see urma_jfr_attr_mask_t */
		uint32_t rx_threshold;
		uint32_t state;
	} in;
	struct uburma_cmd_udrv_priv udata;
};

struct uburma_cmd_query_jfr {
	struct {
		uint64_t handle; /* handle of the allocated jfr obj in kernel */
	} in;
	struct {
		uint32_t depth;
		uint32_t flag;
		uint32_t trans_mode;
		uint8_t max_sge;
		uint8_t min_rnr_timer;
		uint32_t token;
		uint32_t id;

		uint32_t rx_threshold;
		uint32_t state;
	} out;
};

struct uburma_cmd_delete_jfr {
	struct {
		uint64_t handle; /* handle of jfr, used to find jfr obj in kernel */
	} in;
	struct {
		uint32_t async_events_reported;
	} out;
};

struct uburma_cmd_delete_jfr_batch {
	struct {
		uint32_t async_events_reported;
		uint32_t bad_jfr_index;
	} out;
	struct {
		uint32_t jfr_num;
		uint64_t jfr_ptr;
	} in;
};

struct uburma_cmd_create_jfs {
	struct {
		uint32_t depth;
		uint32_t flag;
		uint32_t trans_mode;
		uint8_t priority;
		uint8_t max_sge;
		uint8_t max_rsge;
		uint32_t max_inline_data;
		uint8_t retry_cnt;
		uint8_t rnr_retry;
		uint8_t err_timeout;
		uint32_t jfc_id;
		uint64_t jfc_handle;
		uint64_t urma_jfs; /* urma jfs pointer */
	} in;
	struct {
		uint32_t id;
		uint32_t depth;
		uint8_t max_sge;
		uint8_t max_rsge;
		uint32_t max_inline_data;
		uint64_t handle; /* handle of the allocated jfs obj in kernel */
	} out;
	struct uburma_cmd_udrv_priv udata;
};

struct uburma_cmd_modify_jfs {
	struct {
		uint64_t handle; /* handle of jfs, used to find jfs obj in kernel */
		uint32_t mask; /* see urma_jfs_attr_mask_t */
		uint32_t state;
	} in;
	struct uburma_cmd_udrv_priv udata;
};

struct uburma_cmd_query_jfs {
	struct {
		uint64_t handle; /* handle of the allocated jfs obj in kernel */
	} in;
	struct {
		uint32_t depth;
		uint32_t flag;
		uint32_t trans_mode;
		uint8_t priority;
		uint8_t max_sge;
		uint8_t max_rsge;
		uint32_t max_inline_data;
		uint8_t retry_cnt;
		uint8_t rnr_retry;
		uint8_t err_timeout;

		uint32_t state;
	} out;
};

struct uburma_cmd_delete_jfs {
	struct {
		uint64_t handle; /* handle of jfs, used to find jfs obj in kernel */
	} in;
	struct {
		uint32_t async_events_reported;
	} out;
};

struct uburma_cmd_delete_jfs_batch {
	struct {
		uint32_t async_events_reported;
		uint32_t bad_jfs_index;
	} out;
	struct {
		uint32_t jfs_num;
		uint64_t jfs_ptr;
	} in;
};

struct uburma_cmd_create_jfc {
	struct {
		uint32_t depth; /* in terms of CQEBB */
		uint32_t flag;
		int jfce_fd;
		uint64_t urma_jfc; /* urma jfc pointer */
		uint32_t ceqn; /* [Optional] event queue id */
	} in;
	struct {
		uint32_t id;
		uint32_t depth;
		uint64_t handle; /* handle of the allocated jfc obj in kernel */
	} out;
	struct uburma_cmd_udrv_priv udata;
};

struct uburma_cmd_modify_jfc {
	struct {
		uint64_t handle; /* handle of jfc, used to find jfc obj in kernel */
		uint32_t mask; /* see urma_jfc_attr_mask_t */
		uint16_t moderate_count;
		uint16_t moderate_period; /* in micro seconds */
	} in;
	struct uburma_cmd_udrv_priv udata;
};

struct uburma_cmd_delete_jfc {
	struct {
		uint64_t handle; /* handle of jfc, used to find jfc obj in kernel */
	} in;
	struct {
		uint32_t comp_events_reported;
		uint32_t async_events_reported;
	} out;
};

struct uburma_cmd_delete_jfc_batch {
	struct {
		uint32_t comp_events_reported;
		uint32_t async_events_reported;
		uint32_t bad_jfc_index;
	} out;
	struct {
		uint32_t jfc_num;
		uint64_t jfc_ptr;
	} in;
};

struct uburma_cmd_create_jfce {
	struct {
		int fd;
	} out;
};

struct uburma_cmd_create_jetty {
	struct {
		uint32_t id; /* user may assign id */
		uint32_t jetty_flag;

		uint32_t jfs_depth;
		uint32_t jfs_flag;
		uint32_t trans_mode;
		uint8_t priority;
		uint8_t max_send_sge;
		uint8_t max_send_rsge;
		uint32_t max_inline_data;
		uint8_t rnr_retry;
		uint8_t err_timeout;
		uint32_t send_jfc_id;
		uint64_t send_jfc_handle; /* handle of the related send jfc */

		uint32_t jfr_depth;
		uint32_t jfr_flag;
		uint8_t max_recv_sge;
		uint8_t min_rnr_timer;

		uint32_t recv_jfc_id;
		uint64_t recv_jfc_handle; /* handle of the related recv jfc */
		uint32_t token;

		uint32_t jfr_id; /* shared jfr */
		uint64_t jfr_handle; /* handle of the shared jfr */

		uint64_t jetty_grp_handle; /* handle of the jetty_grp */
		uint8_t is_jetty_grp;

		uint64_t urma_jetty; /* urma jetty pointer */
	} in;
	struct {
		uint32_t id; /* jetty id allocated by ubcore */
		uint64_t handle; /* handle of the allocated jetty obj in kernel */
		uint32_t jfs_depth;
		uint32_t jfr_depth;
		uint8_t max_send_sge;
		uint8_t max_send_rsge;
		uint8_t max_recv_sge;
		uint32_t max_inline_data;
	} out;
	struct uburma_cmd_udrv_priv udata;
};

struct uburma_cmd_modify_jetty {
	struct {
		uint64_t handle; /* handle of jetty, used to find jetty obj in kernel */
		uint32_t mask; /* see urma_jetty_attr_mask_t */
		uint32_t rx_threshold;
		uint32_t state;
	} in;
	struct uburma_cmd_udrv_priv udata;
};

struct uburma_cmd_query_jetty {
	struct {
		uint64_t handle; /* handle of the allocated jetty obj in kernel */
	} in;
	struct {
		uint32_t id; /* user may assign id */
		uint32_t jetty_flag;

		uint32_t jfs_depth;
		uint32_t jfr_depth;
		uint32_t jfs_flag;
		uint32_t jfr_flag;
		uint32_t trans_mode;
		uint8_t max_send_sge;
		uint8_t max_send_rsge;
		uint8_t max_recv_sge;
		uint32_t max_inline_data;
		uint8_t priority;
		uint8_t retry_cnt;
		uint8_t rnr_retry;
		uint8_t err_timeout;
		uint8_t min_rnr_timer;
		uint32_t jfr_id;
		uint32_t token;

		uint32_t rx_threshold;
		uint32_t state;
	} out;
};

struct uburma_cmd_delete_jetty {
	struct {
		uint64_t handle; /* handle of jetty, used to find jetty obj in kernel */
	} in;
	struct {
		uint32_t async_events_reported;
	} out;
};

struct uburma_cmd_delete_jetty_batch {
	struct {
		uint32_t async_events_reported;
		uint32_t bad_jetty_index;
	} out;
	struct {
		uint32_t jetty_num;
		uint64_t jetty_ptr;
	} in;
};

struct uburma_cmd_create_jetty_grp {
	struct {
		char name[UBCORE_JETTY_GRP_MAX_NAME];
		uint32_t token;
		uint32_t id;
		uint32_t policy;
		uint32_t flag;
		uint64_t urma_jetty_grp; /* urma jetty group pointer */
	} in;
	struct {
		uint32_t id; /* jetty group id allocated by ubcore */
		uint64_t handle; /* handle of the allocated jetty group obj in kernel */
	} out;
	struct uburma_cmd_udrv_priv udata;
};

struct uburma_cmd_delete_jetty_grp {
	struct {
		uint64_t handle; /* handle of jetty group, used to find jetty group obj in kernel */
	} in;
	struct {
		uint32_t async_events_reported;
	} out;
};

/* copy from user_space addr to kernel args */
static inline int uburma_copy_from_user(void *args, const void *args_addr,
					unsigned long args_size)
{
	int ret;

	ret = (int)copy_from_user(args, args_addr, args_size);
	if (ret != 0) {
		uburma_log_err("copy from user failed, ret:%d.\n", ret);
		return -EFAULT;
	}
	return 0;
}

/* copy kernel args to user_space addr */
static inline int uburma_copy_to_user(void *args_addr, const void *args,
				      unsigned long args_size)
{
	int ret;

	ret = (int)copy_to_user(args_addr, args, args_size);
	if (ret != 0) {
		uburma_log_err("copy to user failed ret:%d.\n", ret);
		return -EFAULT;
	}
	return 0;
}

int uburma_unimport_jetty(struct uburma_file *file, bool async,
			  int tjetty_handle);
int uburma_unbind_jetty(struct uburma_file *file, bool async, int jetty_handle,
			int tjetty_handle);

void uburma_cmd_inc(struct uburma_device *ubu_dev);
void uburma_cmd_dec(struct uburma_device *ubu_dev);
void uburma_cmd_flush(struct uburma_device *ubu_dev);

#endif /* UBURMA_CMD_H */

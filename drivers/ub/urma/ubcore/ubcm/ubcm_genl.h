/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 *
 * Description: ubcm generic netlink header
 * Author: Chen Yutao
 * Create: 2025-01-10
 * Note:
 * History: 2025-01-10: Create file
 */

#ifndef UBCM_GENL_H
#define UBCM_GENL_H

#include <linux/types.h>



#include "ub_mad.h"

/* NETLINK_GENERIC related info */
#define UBCM_GENL_FAMILY_NAME "UBCM_GENL"
#define UBCM_GENL_FAMILY_VERSION 1
#define UBCM_GENL_INVALID_PORT 0
#define UBCM_MAX_NL_MSG_BUF_LEN 1024
#define UBCM_EID_TABLE_SIZE 256 /* Refer to TPSA_EID_IDX_TABLE_SIZE */

#define UBCM_MAX_UVS_NAME_LEN 64

enum ubcm_uvs_state { UBCM_UVS_STATE_DEAD = 0, UBCM_UVS_STATE_ALIVE };

enum ubcm_genl_attr { /* Refer to enum uvs_cm_genl_attr */
		      UBCM_ATTR_UNSPEC,
		      UBCM_HDR_COMMAND,
		      UBCM_HDR_ARGS_LEN,
		      UBCM_HDR_ARGS_ADDR,
		      UBCM_ATTR_NS_MODE,
		      UBCM_ATTR_DEV_NAME,
		      UBCM_ATTR_NS_FD,
		      UBCM_MSG_SEQ,
		      UBCM_MSG_TYPE,
		      UBCM_SRC_ID,
		      UBCM_DST_ID,
		      UBCM_RESERVED,
		      UBCM_PAYLOAD_DATA,
		      UBCM_ATTR_AFTER_LAST,
		      NUM_UBCM_ATTR = UBCM_ATTR_AFTER_LAST,
		      UBCM_ATTR_MAX = UBCM_ATTR_AFTER_LAST - 1
};

/* Handling generic netlnik messages from UVS, only forward messages */
enum ubcm_genl_msg_type {
	UBCM_CMD_UVS_ADD = 0,
	UBCM_CMD_UVS_REMOVE,
	UBCM_CMD_UVS_ADD_EID,
	UBCM_CMD_UVS_DEL_EID,
	UBCM_CMD_UVS_MSG,
	UBCM_CMD_UVS_AUTHN, /* Authentication */
	UBCM_CMD_NUM
};

struct ubcm_nlmsg { /* Refer to uvs_nl_cm_msg_t */
	uint32_t nlmsg_seq;
	uint32_t msg_type; /* Refer to ubcm_genl_msg_type */
	union ubcore_eid src_eid;
	union ubcore_eid dst_eid;
	uint32_t payload_len;
	uint32_t reserved;
	uint8_t payload[];
};

struct ubcm_uvs_eid_node {
	struct hlist_node node;
	uint32_t eid_idx;
	uint32_t reserved;
	union ubcore_eid eid;
};

struct ubcm_uvs_genl_node {
	struct list_head list_node;
	struct kref ref;
	char name[UBCM_MAX_UVS_NAME_LEN]; /* name to identify UVS */
	enum ubcm_uvs_state state;
	uint32_t id;
	uint32_t policy;
	uint32_t genl_port; /* uvs genl port */
	struct sock *genl_sock;
	uint32_t pid;
	atomic_t map2ue;
	atomic_t nl_wait_buffer;
	struct hlist_head eid_hlist
		[UBCM_EID_TABLE_SIZE]; /* Storing struct ubcm_uvs_eid_node */
	uint32_t eid_cnt;
};

/* Payload structure for UBCM_CMD_UVS_ADD_EID or UBCM_CMD_UVS_DEL_EID */
struct ubcm_nlmsg_op_eid {
	uint32_t eid_idx;
	uint32_t reserved;
	union ubcore_eid eid;
	char uvs_name[UBCM_MAX_UVS_NAME_LEN];
};

extern atomic_t g_ubcm_nlmsg_seq;
static inline uint32_t ubcm_get_nlmsg_seq(void)
{
	return atomic_inc_return(&g_ubcm_nlmsg_seq);
}

int ubcm_genl_init(void);
void ubcm_genl_uninit(void);

struct ubcm_nlmsg *ubcm_alloc_genl_msg(struct ubmad_recv_cr *recv_cr);
struct ubcm_nlmsg *ubcm_alloc_genl_authn_msg(struct ubmad_recv_cr *recv_cr);

void ubcm_uvs_kref_get(struct ubcm_uvs_genl_node *node);
void ubcm_uvs_kref_put(struct ubcm_uvs_genl_node *node);

struct ubcm_uvs_genl_node *ubcm_find_get_uvs_by_eid(union ubcore_eid *eid);

static inline uint32_t ubcm_nlmsg_len(struct ubcm_nlmsg *msg)
{
	return sizeof(struct ubcm_nlmsg) + msg->payload_len;
}

/* Ubcm send nlmsg to UVS by netlink */
int ubcm_genl_unicast(struct ubcm_nlmsg *msg, uint32_t len,
		      struct ubcm_uvs_genl_node *uvs);

#endif /* UBCM_GENL_H */

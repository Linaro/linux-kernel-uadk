// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 *
 * Description: ubcore net implementation
 * Author: Wang Hang
 * Create: 2025-06-09
 * Note:
 * History: 2025-06-09: Create file
 */

#include "ubcore_sock.h"
#include "ubcore_cm.h"
#include "ubcore_log.h"
#include "ubcore_net.h"

static const char *const msg_type_str_list[UBCORE_NET_MSG_MAX] = {
	[UBCORE_NET_CREATE_REQ] = "create-req",
	[UBCORE_NET_CREATE_RESP] = "create-resp",
	[UBCORE_NET_CREATE_ACK] = "create-ack",
	[UBCORE_NET_DESTROY_REQ] = "destroy-req",
	[UBCORE_NET_DESTROY_RESP] = "destroy-resp",
	[UBCORE_NET_BONDING_SEG_INFO_REQ] = "seg_req",
	[UBCORE_NET_BONDING_SEG_INFO_RESP] = "seg_resp",
	[UBCORE_NET_BONDING_JETTY_INFO_REQ] = "jetty_req",
	[UBCORE_NET_BONDING_JETTY_INFO_RESP] = "jetty_resp",
};

enum ubcore_connect_type {
	UBCORE_CONNECT_WK_JETTY = 0U, /* well-known jetty */
	UBCORE_CONNECT_SOCK,
};

uint32_t g_ubcore_connect_type = UBCORE_CONNECT_WK_JETTY;

const char *msg_type_str(enum ubcore_net_msg_type type)
{
	size_t index = type;

	return (index < UBCORE_NET_MSG_MAX) ? msg_type_str_list[index] :
						    "unknown";
}

struct ubcore_net_msg_descriptor {
	ubcore_net_msg_handler handler;
	uint16_t msg_len;
};

static struct ubcore_net_msg_descriptor g_descriptors[UBCORE_NET_MSG_MAX] = {
	0
};

int ubcore_net_register_msg_handler(enum ubcore_net_msg_type type,
				    ubcore_net_msg_handler handler,
				    uint16_t msg_len)
{
	size_t index = type;

	if (index >= UBCORE_NET_MSG_MAX ||
	    g_descriptors[index].handler != NULL) {
		ubcore_log_err("Failed to register net handler, type:%s",
			       msg_type_str(type));
		return -EINVAL;
	}
	g_descriptors[index].handler = handler;
	g_descriptors[index].msg_len = msg_len;
	return 0;
}

void ubcore_net_handle_msg(struct ubcore_device *dev,
			   struct ubcore_net_msg *msg, void *conn)
{
	struct ubcore_net_msg_descriptor *descriptor = NULL;

	if (msg->type >= UBCORE_NET_MSG_MAX) {
		ubcore_log_err("Invalid net msg type, " MSG_FMT, MSG_ARG(msg));
		return;
	}

	descriptor = &g_descriptors[msg->type];
	if (msg->len != descriptor->msg_len) {
		ubcore_log_err("Inalid net msg len, expected: %u, " MSG_FMT,
			       descriptor->msg_len, MSG_ARG(msg));
		return;
	}
	if (!descriptor->handler) {
		ubcore_log_err("No handler for net msg, " MSG_FMT,
			       MSG_ARG(msg));
		return;
	}

	descriptor->handler(dev, msg, conn);
}

static bool ubcore_is_loopback(struct ubcore_device *dev,
			       union ubcore_eid *addr)
{
	uint32_t eid_idx;

	spin_lock(&dev->eid_table.lock);
	for (eid_idx = 0; eid_idx < dev->eid_table.eid_cnt; eid_idx++) {
		if (dev->eid_table.eid_entries[eid_idx].valid &&
		    memcmp(addr, &dev->eid_table.eid_entries[eid_idx].eid,
			   sizeof(union ubcore_eid)) == 0) {
			spin_unlock(&dev->eid_table.lock);
			return true;
		}
	}
	spin_unlock(&dev->eid_table.lock);
	return false;
}

int ubcore_net_send(struct ubcore_device *dev, struct ubcore_net_msg *msg,
		    void *conn)
{
	if (!conn) {
		ubcore_log_info("Loopback detected, using shortcut.");
		ubcore_net_handle_msg(dev, msg, NULL);
		return 0;
	}

	switch (g_ubcore_connect_type) {
	case UBCORE_CONNECT_WK_JETTY:
		return ubcore_ubcm_send(dev, conn, msg);
	case UBCORE_CONNECT_SOCK:
		return ubcore_sock_send(dev, conn, msg);
	default:
		ubcore_log_err("connect type unrecognized!");
		return -EINVAL;
	}
}

int ubcore_net_send_to(struct ubcore_device *dev, struct ubcore_net_msg *msg,
		       union ubcore_eid addr)
{
	if (ubcore_is_loopback(dev, &addr)) {
		ubcore_log_info("Loopback detected, using shortcut.");
		ubcore_net_handle_msg(dev, msg, NULL);
		return 0;
	}

	switch (g_ubcore_connect_type) {
	case UBCORE_CONNECT_WK_JETTY:
		return ubcore_ubcm_send_to(dev, addr, msg);
	case UBCORE_CONNECT_SOCK:
		return ubcore_sock_send_to(dev, msg, addr);
	default:
		ubcore_log_err("connect type unrecognized!");
		return -EINVAL;
	}
}

int ubcore_net_comm_init(void)
{
	if (ubcore_session_init() != 0) {
		ubcore_log_err("Failed to init session service");
		return -EINVAL;
	}
	if (ubcore_sock_init() != 0) {
		ubcore_log_err("connect type unrecognized!");
		goto uninit_session;
	}
	return 0;

uninit_session:
	ubcore_session_uninit();
	return -EINVAL;
}

void ubcore_net_comm_uninit(void)
{
	ubcore_sock_uninit();
	ubcore_session_uninit();
}

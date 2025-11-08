// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 *
 * Description: ubcore connection manager implementation, interacts with ubcm
 * Author: Wang Hang
 * Create: 2025-02-18
 * Note:
 * History: 2025-02-18: create file
 */

#include "ubcore_log.h"
#include "ubcore_cm.h"

static ubcore_cm_eid_ops g_eid_ops;
static ubcore_cm_send g_send;

struct cm_entry {
	union ubcore_eid addr;
};

static int ubcore_call_cm_send_ops(struct ubcore_device *dev,
				   struct ubcore_cm_send_buf *send_buf)
{
	if (!g_send) {
		ubcore_log_err("ubcore_cm_send_ops function not registered yet!");
		return -EINVAL;
	}

	return g_send(dev, send_buf);
}

int ubcore_call_cm_eid_ops(struct ubcore_device *dev,
			   struct ubcore_eid_info *eid_info,
			   enum ubcore_mgmt_event_type event_type)
{
	if (!g_eid_ops) {
		ubcore_log_err("ubcore_cm_eid_ops function not registered yet!");
		return -EINVAL;
	}

	return g_eid_ops(dev, eid_info, event_type);
}

void ubcore_register_cm_eid_ops(ubcore_cm_eid_ops eid_ops)
{
	g_eid_ops = eid_ops;
	ubcore_log_info("ubcore_cm_eid_ops function registered!");
}

void ubcore_register_cm_send_ops(ubcore_cm_send cm_send)
{
	g_send = cm_send;
	ubcore_log_info("ubcore_cm_send_ops function registered!");
}

int ubcore_cm_recv(struct ubcore_device *dev, struct ubcore_cm_recv_cr *recv_cr)
{
	union ubcore_eid addr = recv_cr->cr->remote_id.eid;
	struct ubcore_net_msg msg = { 0 };

	(void)memcpy(&msg, (void *)(recv_cr->payload), MSG_HDR_SIZE);
	msg.data = (void *)(recv_cr->payload + MSG_HDR_SIZE);

	ubcore_log_info("Handle cm message, " MSG_FMT, MSG_ARG(&msg));
	ubcore_net_handle_msg(dev, &msg, &addr);

	return 0;
}

int ubcore_ubcm_send_to(struct ubcore_device *dev, union ubcore_eid addr,
			struct ubcore_net_msg *msg)
{
	uint16_t send_buf_len =
		sizeof(struct ubcore_cm_send_buf) + MSG_HDR_SIZE + msg->len;
	struct ubcore_cm_send_buf *send_buf = NULL;
	int ret;

	ubcore_log_info("Send cm message, " MSG_FMT, MSG_ARG(msg));

	send_buf = kcalloc(1, send_buf_len, GFP_KERNEL);
	if (IS_ERR_OR_NULL(send_buf)) {
		ubcore_log_err("Failed to alloc cm send buf memory.\n");
		return -ENOMEM;
	}

	send_buf->dst_eid = addr;
	send_buf->msg_type = UBCORE_CM_CONN_MSG;
	send_buf->payload_len = send_buf_len;
	(void)memcpy(send_buf->payload, msg, MSG_HDR_SIZE);
	(void)memcpy(send_buf->payload + MSG_HDR_SIZE, msg->data, msg->len);

	ret = ubcore_call_cm_send_ops(dev, send_buf);
	if (ret != 0)
		ubcore_log_err("Failed to send cm message, ret:%d, " MSG_FMT,
			       ret, MSG_ARG(msg));

	kfree(send_buf);
	return ret;
}

int ubcore_ubcm_send(struct ubcore_device *dev, void *conn,
		     struct ubcore_net_msg *msg)
{
	return ubcore_ubcm_send_to(dev, *(union ubcore_eid *)conn, msg);
}

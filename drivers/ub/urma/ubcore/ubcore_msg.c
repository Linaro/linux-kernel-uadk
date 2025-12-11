// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2025. All rights reserved.
 *
 * Description: ubcore message table implementation
 * Author: Yang Yijian
 * Create: 2023-07-05
 * Note:
 * History: 2023-07-05: Create file
 */

#include <linux/slab.h>
#include <linux/time64.h>
#include <linux/timekeeping.h>
#include <ub/urma/ubcore_types.h>
#include <ub/urma/ubcore_api.h>
#include <ub/urma/ubcore_uapi.h>
#include "ubcore_log.h"
#include "ubcore_netlink.h"
#include "ubcore_vtp.h"
#include "ubcore_priv.h"
#include "ubcore_workqueue.h"
#include "ubcore_main.h"
#include "ubcore_device.h"
#include "ubcore_msg.h"

#define MS_PER_SEC 1000
static LIST_HEAD(g_msg_session_list);
static DEFINE_SPINLOCK(g_msg_session_lock);
static atomic_t g_msg_seq = ATOMIC_INIT(0);

static uint32_t ubcore_get_msg_seq(void)
{
	return (uint32_t)atomic_inc_return(&g_msg_seq);
}

void ubcore_free_msg_session(struct kref *kref)
{
	struct ubcore_msg_session *s =
		container_of(kref, struct ubcore_msg_session, kref);
	unsigned long flags;

	spin_lock_irqsave(&g_msg_session_lock, flags);
	list_del(&s->node);
	spin_unlock_irqrestore(&g_msg_session_lock, flags);

	mutex_destroy(&s->session_lock);
	kfree(s);
}

struct ubcore_msg_session *ubcore_find_msg_session(uint32_t seq)
{
	struct ubcore_msg_session *tmp, *target = NULL;
	unsigned long flags;

	spin_lock_irqsave(&g_msg_session_lock, flags);
	list_for_each_entry(tmp, &g_msg_session_list, node) {
		if (tmp->req == NULL)
			continue;
		if (tmp->req->msg_id == seq) {
			target = tmp;
			kref_get(&target->kref);
			break;
		}
	}
	spin_unlock_irqrestore(&g_msg_session_lock, flags);
	return target;
}

void ubcore_destroy_msg_session(struct ubcore_msg_session *s)
{
	(void)kref_put(&s->kref, ubcore_free_msg_session);
}

static struct ubcore_msg_session *
ubcore_create_msg_session(struct ubcore_req *req)
{
	struct ubcore_msg_session *s;
	unsigned long flags;

	s = kzalloc(sizeof(struct ubcore_msg_session), GFP_KERNEL);
	if (s == NULL)
		return NULL;

	s->req = req;
	s->resp = NULL;
	s->is_async = false;
	s->vtpn = NULL;
	s->msg_id = req->msg_id;
	mutex_init(&s->session_lock);
	s->session_state = UBCORE_SESSION_INIT;
	kref_init(&s->kref);
	init_completion(&s->comp);
	spin_lock_irqsave(&g_msg_session_lock, flags);
	kref_get(&s->kref);
	list_add_tail(&s->node, &g_msg_session_list);
	spin_unlock_irqrestore(&g_msg_session_lock, flags);
	return s;
}

bool ubcore_set_session_finish(struct ubcore_msg_session *s)
{
	if (s->session_state == UBCORE_SESSION_FINISH)
		return false;

	if (!mutex_trylock(&s->session_lock))
		return false;

	if (s->session_state == UBCORE_SESSION_FINISH) {
		mutex_unlock(&s->session_lock);
		return false;
	}
	s->session_state = UBCORE_SESSION_FINISH;
	mutex_unlock(&s->session_lock);
	return true;
}

int ubcore_send_req(struct ubcore_device *dev, struct ubcore_req *req)
{
	int ret;

	if (dev == NULL || dev->ops == NULL || dev->ops->send_req == NULL ||
	    req->len > UBCORE_MAX_MSG) {
		ubcore_log_err("Invalid parameter!\n");
		return -EINVAL;
	}

	ret = dev->ops->send_req(dev, req);
	if (ret != 0) {
		ubcore_log_err("Failed to send message! msg_id = %u!\n",
			       req->msg_id);
		return ret;
	}
	return 0;
}

int ubcore_send_resp(struct ubcore_device *dev,
		     struct ubcore_resp_host *resp_host)
{
	int ret;

	if (dev == NULL || dev->ops == NULL || dev->ops->send_resp == NULL ||
	    resp_host == NULL || resp_host->resp.len > UBCORE_MAX_MSG) {
		ubcore_log_err("Invalid parameter!\n");
		return -EINVAL;
	}

	ret = dev->ops->send_resp(dev, resp_host);
	if (ret != 0) {
		ubcore_log_err("Failed to send message! msg_id = %u!\n",
			       resp_host->resp.msg_id);
		return ret;
	}
	return 0;
}

struct ubcore_msg_session *
ubcore_create_ue2mue_session(struct ubcore_req *req, struct ubcore_vtpn *vtpn)
{
	struct ubcore_msg_session *s;

	req->msg_id = ubcore_get_msg_seq();
	s = ubcore_create_msg_session(req);
	if (s == NULL) {
		ubcore_log_err("Failed to create req session!\n");
		return NULL;
	}
	s->is_async = true;
	s->vtpn = vtpn;
	return s;
}

static int ubcore_msg_discover_eid_cb(struct ubcore_device *dev,
				      struct ubcore_resp *resp, void *msg_ctx)
{
	struct ubcore_msg_discover_eid_resp *data;
	struct net *net = (struct net *)msg_ctx;
	bool is_alloc_eid;

	if (dev == NULL || resp == NULL ||
	    resp->len < sizeof(struct ubcore_msg_discover_eid_resp)) {
		ubcore_log_err("Invalid parameter.\n");
		return -EINVAL;
	}
	data = (struct ubcore_msg_discover_eid_resp *)(void *)resp->data;
	if (data == NULL || data->ret != 0 ||
	    (resp->opcode != UBCORE_MSG_ALLOC_EID &&
	     resp->opcode != UBCORE_MSG_DEALLOC_EID)) {
		ubcore_log_err(
			"Failed to query data from the UVS. Use the default value.\n");
		return -EINVAL;
	}

	is_alloc_eid = (resp->opcode == UBCORE_MSG_ALLOC_EID);
	if (ubcore_update_eidtbl_by_idx(dev, &data->eid, data->eid_index,
					is_alloc_eid, net) != 0)
		return -1;

	return 0;
}

/**
 *	If you do not need to wait for the response of a message, use ubcore_asyn_send_ue2mue_msg.
 */
struct ubcore_msg_session *
ubcore_asyn_send_ue2mue_msg(struct ubcore_device *dev, struct ubcore_req *req)
{
	struct ubcore_msg_session *s;
	int ret;

	req->msg_id = ubcore_get_msg_seq();
	s = ubcore_create_msg_session(req);
	if (s == NULL) {
		ubcore_log_err("Failed to create req session!\n");
		return NULL;
	}

	ret = ubcore_send_req(dev, req);
	(void)kref_put(&s->kref, ubcore_free_msg_session);
	if (ret != 0) {
		ubcore_log_err(
			"Failed to send req, msg_id = %u, opcode = %u.\n",
			req->msg_id, (uint16_t)req->opcode);
		ubcore_destroy_msg_session(s);
		return NULL;
	}
	return s;
}

int ubcore_msg_discover_eid(struct ubcore_device *dev, uint32_t eid_index,
			    enum ubcore_msg_opcode op, struct net *net,
			    struct ubcore_update_eid_ctx *ctx)
{
	struct ubcore_msg_discover_eid_req *data;
	struct ubcore_msg_session *s;
	struct ubcore_req *req_msg;
	uint32_t data_len;

	ctx->cb.callback = ubcore_msg_discover_eid_cb;
	ctx->cb.user_arg = net;
	data_len = sizeof(struct ubcore_msg_discover_eid_req);
	req_msg = kcalloc(1, sizeof(struct ubcore_req) + data_len, GFP_KERNEL);
	if (req_msg == NULL)
		return -ENOMEM;

	req_msg->len = data_len;
	req_msg->opcode = op;
	data = (struct ubcore_msg_discover_eid_req *)req_msg->data;
	data->eid_index = eid_index;
	data->eid_type = dev->attr.pattern;
	data->virtualization = dev->attr.virtualization;
	memcpy(data->dev_name, dev->dev_name, UBCORE_MAX_DEV_NAME);

	s = ubcore_asyn_send_ue2mue_msg(dev, req_msg);
	if (s == NULL) {
		ubcore_log_err("send ue2mue failed.\n");
		kfree(req_msg);
		return -1;
	}
	ctx->req_msg = req_msg;
	ctx->s = s;
	return 0;
}

/**
 *	if the operation times out or is successful, 0 is returned and reply done  to urma_admin.
 *	if the operation is waiting for the result, 1 is returned  and reply dump to urma_admin.
 */
int ubcore_update_uvs_eid_ret(struct ubcore_update_eid_ctx *ctx)
{
	long start_ts = ctx->start_ts;
	long leave_time = 0;
	struct timespec64 tv;
	bool is_done;

	is_done = try_wait_for_completion(&ctx->s->comp);
	if (is_done == false) {
		ktime_get_ts64(&tv);
		leave_time = tv.tv_sec - start_ts;
		if (leave_time * MS_PER_SEC < UBCORE_TYPICAL_TIMEOUT)
			return 1;

		ubcore_log_err(
			"waiting req reply timeout, msg_id = %u, opcode = %u, leavetime =  %ld.\n",
			ctx->req_msg->msg_id, (uint16_t)ctx->req_msg->opcode,
			leave_time);
		return -EAGAIN;
	}

	ubcore_log_info("waiting req reply success, msg_id = %u, opcode = %u\n",
			ctx->req_msg->msg_id, (uint16_t)ctx->req_msg->opcode);

	if (ctx->cb.callback(ctx->dev, ctx->s->resp, ctx->cb.user_arg) != 0)
		return -EINVAL;

	return 0;
}

int ubcore_recv_req(struct ubcore_device *dev, struct ubcore_req_host *req)
{
	return 0;
}
EXPORT_SYMBOL(ubcore_recv_req);

int ubcore_recv_resp(struct ubcore_device *dev, struct ubcore_resp *resp)
{
	return 0;
}
EXPORT_SYMBOL(ubcore_recv_resp);

// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 *
 * Description: ubcore connect bonding implementation file
 * Author: Wang Hang
 * Create: 2025-08-07
 * Note:
 * History: 2025-08-07: create file
 */

#include <ub/urma/ubcore_uapi.h>
#include "ubcore_connect_bonding.h"
#include "net/ubcore_net.h"
#include "ubcore_priv.h"
#include "ubcore_topo_info.h"
#include "ubcore_log.h"

#define BONDING_UDATA_BUF_LEN 960

struct session_data_exchange_udata {
	int *result;
	char *udata_out;
	uint32_t udata_out_size;
};

struct msg_seg_info_req {
	struct ubcore_ubva ubva;
	uint64_t len;
	uint32_t token_id;
};

struct msg_jetty_info_req {
	struct ubcore_jetty_id jetty_id;
	bool is_jfr;
};

struct msg_seg_info_resp {
	int result;
	char seg_info[BONDING_UDATA_BUF_LEN];
};

struct msg_jetty_info_resp {
	int result;
	char jetty_info[BONDING_UDATA_BUF_LEN];
};

static struct ubcore_device *ubcore_find_physical_device(void)
{
	struct ubcore_topo_map *topo_map;
	struct ubcore_topo_info *topo_info;
	union ubcore_eid *primary_eid;

	topo_map = ubcore_get_global_topo_map();
	if (!topo_map) {
		ubcore_log_err("Failed get global topo map");
		return NULL;
	}

	topo_info = ubcore_get_cur_topo_info(topo_map);
	if (!topo_info) {
		ubcore_log_err("Failed get global topo info");
		return NULL;
	}

	primary_eid = (union ubcore_eid *)topo_info->io_die_info[0].primary_eid;
	return ubcore_find_device(primary_eid, UBCORE_TRANSPORT_UB);
}

static struct ubcore_device *ubcore_find_bonding_device(void)
{
	struct ubcore_topo_map *topo_map;
	struct ubcore_topo_info *topo_info;
	union ubcore_eid *bonding_eid;

	topo_map = ubcore_get_global_topo_map();
	if (!topo_map) {
		ubcore_log_err("Failed get global topo map");
		return NULL;
	}

	topo_info = ubcore_get_cur_topo_info(topo_map);
	if (!topo_info) {
		ubcore_log_err("Failed get global topo info");
		return NULL;
	}

	bonding_eid = (union ubcore_eid *)topo_info->bonding_eid;
	return ubcore_find_device(bonding_eid, UBCORE_TRANSPORT_UB);
}

static struct ubcore_session *
create_session_for_exchange_udata(struct ubcore_device *dev,
			int *result, char *udata_out, uint32_t udata_out_size)
{
	struct ubcore_session *session;
	struct session_data_exchange_udata *session_data;

	session_data =
		kzalloc(sizeof(struct session_data_exchange_udata), GFP_KERNEL);
	if (IS_ERR_OR_NULL(session_data)) {
		ubcore_log_err("Failed to alloc exchange seg info user arg");
		return NULL;
	}
	session_data->result = result;
	session_data->udata_out = udata_out;
	session_data->udata_out_size = udata_out_size;

	session = ubcore_session_create(dev, session_data, 0, NULL, NULL);
	if (!session) {
		ubcore_log_err("Failed to alloc session for exchange seg info");
		kfree(session_data);
		return NULL;
	}

	return session;
}

static int send_seg_info_req(struct ubcore_device *dev, uint32_t session_id,
			     struct msg_seg_info_req *req)
{
	struct ubcore_net_msg msg = { 0 };
	union ubcore_eid dest_eid = { 0 };
	int ret;

	msg.type = UBCORE_NET_BONDING_SEG_INFO_REQ;
	msg.len = sizeof(struct msg_seg_info_req);
	msg.session_id = session_id;
	msg.data = req;

	ret = ubcore_get_primary_eid_by_bonding_eid(&req->ubva.eid, &dest_eid);
	if (ret != 0)
		return ret;

	ubcore_log_info("Send seg info req to " EID_FMT "\n",
			EID_ARGS(dest_eid));
	ret = ubcore_net_send_to(dev, &msg, dest_eid);
	if (ret != 0) {
		ubcore_log_err("Failed to send msg");
		return ret;
	}
	return 0;
}

static int send_seg_info_resp(struct ubcore_device *dev, void *conn,
			      uint32_t session_id,
			      struct msg_seg_info_resp *resp)
{
	struct ubcore_net_msg msg = { 0 };
	int ret;

	msg.type = UBCORE_NET_BONDING_SEG_INFO_RESP;
	msg.len = sizeof(struct msg_seg_info_resp);
	msg.session_id = session_id;
	msg.data = resp;

	ret = ubcore_net_send(dev, &msg, conn);
	if (ret != 0) {
		ubcore_log_err("Failed to send msg");
		return ret;
	}
	return 0;
}

static int send_jetty_info_req(struct ubcore_device *dev, uint32_t session_id,
			       struct msg_jetty_info_req *req)
{
	struct ubcore_net_msg msg = { 0 };
	union ubcore_eid dest_eid = { 0 };
	int ret;

	msg.type = UBCORE_NET_BONDING_JETTY_INFO_REQ;
	msg.len = sizeof(struct msg_jetty_info_req);
	msg.session_id = session_id;
	msg.data = req;

	ret = ubcore_get_primary_eid_by_bonding_eid(&req->jetty_id.eid,
						    &dest_eid);
	if (ret != 0)
		return ret;

	ubcore_log_info("Send jetty info req to " EID_FMT "\n",
			EID_ARGS(dest_eid));
	ret = ubcore_net_send_to(dev, &msg, dest_eid);
	if (ret != 0) {
		ubcore_log_err("Failed to send msg");
		return ret;
	}
	return 0;
}

static int send_jetty_info_resp(struct ubcore_device *dev, void *conn,
				uint32_t session_id,
				struct msg_jetty_info_resp *resp)
{
	struct ubcore_net_msg msg = { 0 };
	int ret;

	msg.type = UBCORE_NET_BONDING_JETTY_INFO_RESP;
	msg.len = sizeof(struct msg_jetty_info_resp);
	msg.session_id = session_id;
	msg.data = resp;

	ret = ubcore_net_send(dev, &msg, conn);
	if (ret != 0) {
		ubcore_log_err("Failed to send msg");
		return ret;
	}
	return 0;
}

int ubcore_connect_exchange_udata_when_import_seg(struct ubcore_seg *seg,
						  struct ubcore_udata *udata)
{
	struct ubcore_device *physical_dev = ubcore_find_physical_device();
	struct msg_seg_info_req req = { 0 };
	struct ubcore_session *session;
	char buf[BONDING_UDATA_BUF_LEN];
	int ret, result = -1;

	if (!physical_dev) {
		ubcore_log_err("Failed find physical device");
		return -EINVAL;
	}

	session = create_session_for_exchange_udata(physical_dev, &result, buf,
						    sizeof(buf));
	if (!session) {
		ret = -ENOMEM;
		goto put_device;
	}

	req.ubva = seg->ubva;
	req.len = seg->len;
	req.token_id = seg->token_id;
	ret = send_seg_info_req(physical_dev, ubcore_session_get_id(session),
				&req);
	if (ret != 0) {
		ubcore_log_err("Failed to send create req message");
		ubcore_session_complete(session);
		goto release_session;
	}
	ubcore_session_wait(session);

	if (result != 0) {
		ubcore_log_err("Failed to exchange udata, ret: %d.\n", result);
		ret = result;
		goto release_session;
	}

	ret = copy_to_user((void __user *)udata->udrv_data->out_addr, buf,
			   udata->udrv_data->out_len);
	if (ret != 0) {
		ubcore_log_err("Failed to copy to user, ret: %d.\n", ret);
		goto release_session;
	}

	ubcore_session_ref_release(session);
	ubcore_put_device(physical_dev);
	return 0;

release_session:
	ubcore_session_ref_release(session);
put_device:
	ubcore_put_device(physical_dev);
	return ret;
}

int ubcore_connect_exchange_udata_when_import_jetty(
	struct ubcore_tjetty_cfg *cfg, struct ubcore_udata *udata, bool is_jfr)
{
	struct ubcore_device *physical_dev = ubcore_find_physical_device();
	struct msg_jetty_info_req req = { 0 };
	struct ubcore_session *session;
	char buf[BONDING_UDATA_BUF_LEN];
	int ret, result = -1;

	if (!physical_dev) {
		ubcore_log_err("Failed find physical device");
		return -EINVAL;
	}

	session = create_session_for_exchange_udata(physical_dev, &result, buf,
						    sizeof(buf));
	if (!session) {
		ret = -ENOMEM;
		goto put_device;
	}

	req.is_jfr = is_jfr;
	req.jetty_id = cfg->id;
	ret = send_jetty_info_req(physical_dev, ubcore_session_get_id(session),
				  &req);
	if (ret != 0) {
		ubcore_log_err("Failed to send create req message");
		ubcore_session_complete(session);
		goto release_session;
	}
	ubcore_session_wait(session);

	if (result != 0) {
		ubcore_log_err("Failed to exchange udata, ret: %d.\n", result);
		ret = result;
		goto release_session;
	}

	ret = copy_to_user((void __user *)udata->udrv_data->out_addr, buf,
			   udata->udrv_data->out_len);
	if (ret != 0) {
		ubcore_log_err("Failed to copy to user, ret: %d.\n", ret);
		goto release_session;
	}

	ubcore_session_ref_release(session);
	ubcore_put_device(physical_dev);
	return 0;

release_session:
	ubcore_session_ref_release(session);
put_device:
	ubcore_put_device(physical_dev);
	return ret;
}

static void handle_seg_info_req(struct ubcore_device *dev,
				struct ubcore_net_msg *msg, void *conn)
{
	struct msg_seg_info_req *req = (struct msg_seg_info_req *)msg->data;
	struct ubcore_device *bonding_dev = ubcore_find_bonding_device();
	int ret = 0;

	struct msg_seg_info_resp resp = { 0 };
	struct ubcore_user_ctl k_user_ctl = {
		.in.opcode = 5,
		.in.addr = (uint64_t)req,
		.in.len = sizeof(*req),
		.out.addr = (uint64_t)(&resp.seg_info),
		.out.len = sizeof(resp.seg_info),
	};

	ret = ubcore_user_control(bonding_dev, &k_user_ctl);
	if (ret != 0)
		ubcore_log_err("Failed to get seg info by user ctl");

	resp.result = ret;
	if (send_seg_info_resp(dev, conn, msg->session_id, &resp) != 0)
		ubcore_log_err("Failed to send create resp message.\n");
}

static void handle_jetty_info_req(struct ubcore_device *dev,
				  struct ubcore_net_msg *msg, void *conn)
{
	struct msg_jetty_info_req *req = (struct msg_jetty_info_req *)msg->data;
	struct ubcore_device *bonding_dev = ubcore_find_bonding_device();
	int ret = 0;

	struct msg_jetty_info_resp resp = { 0 };
	struct ubcore_user_ctl k_user_ctl = {
		.in.opcode = 6,
		.in.addr = (uint64_t)req,
		.in.len = sizeof(*req),
		.out.addr = (uint64_t)(&resp.jetty_info),
		.out.len = sizeof(resp.jetty_info),
	};

	ret = ubcore_user_control(bonding_dev, &k_user_ctl);
	if (ret != 0)
		ubcore_log_err("Failed to get jetty info by user ctl");

	resp.result = ret;
	if (send_jetty_info_resp(dev, conn, msg->session_id, &resp) != 0)
		ubcore_log_err("Failed to send create resp message.\n");
}

static void handle_exchange_udata_resp(struct ubcore_device *dev, void *conn,
				       uint32_t session_id, int result,
				       void *data)
{
	struct ubcore_session *session;
	struct session_data_exchange_udata *session_data;

	session = ubcore_session_find(session_id);
	if (!session) {
		ubcore_log_err(
			"Failed to find session %u on handle bonding-seg-info-req",
			session_id);
		return;
	}
	session_data =
		(struct session_data_exchange_udata *)ubcore_session_get_data(
			session);

	if (result != 0) {
		*session_data->result = result;
		ubcore_log_err("Failed to exchange udata, ret: %d.\n", result);
		goto complete_session;
	}

	memcpy(session_data->udata_out, data, session_data->udata_out_size);
	*session_data->result = 0;
	ubcore_log_info("Create response result: %d.\n", result);

complete_session:
	ubcore_session_complete(session);
	ubcore_session_ref_release(session);
}

static void handle_seg_info_resp(struct ubcore_device *dev,
				 struct ubcore_net_msg *msg, void *conn)
{
	struct msg_seg_info_resp *resp = (struct msg_seg_info_resp *)msg->data;

	handle_exchange_udata_resp(dev, conn, msg->session_id, resp->result,
				   resp->seg_info);
}

static void handle_jetty_info_resp(struct ubcore_device *dev,
				   struct ubcore_net_msg *msg, void *conn)
{
	struct msg_jetty_info_resp *resp =
		(struct msg_jetty_info_resp *)msg->data;

	handle_exchange_udata_resp(dev, conn, msg->session_id, resp->result,
				   &resp->jetty_info);
}

void ubcore_connect_bonding_init(void)
{
	ubcore_net_register_msg_handler(UBCORE_NET_BONDING_SEG_INFO_REQ,
					handle_seg_info_req,
					sizeof(struct msg_seg_info_req));
	ubcore_net_register_msg_handler(UBCORE_NET_BONDING_SEG_INFO_RESP,
					handle_seg_info_resp,
					sizeof(struct msg_seg_info_resp));
	ubcore_net_register_msg_handler(UBCORE_NET_BONDING_JETTY_INFO_REQ,
					handle_jetty_info_req,
					sizeof(struct msg_jetty_info_req));
	ubcore_net_register_msg_handler(UBCORE_NET_BONDING_JETTY_INFO_RESP,
					handle_jetty_info_resp,
					sizeof(struct msg_jetty_info_resp));
}

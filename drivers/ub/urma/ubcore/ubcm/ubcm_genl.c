// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 *
 * Description: ub_cm generic netlink implementation
 * Author: Chen Yutao
 * Create: 2025-01-10
 * Note:
 * History: 2025-01-10: create file
 */

#include <net/netlink.h>
#include <net/genetlink.h>
#include <linux/version.h>
#include <linux/jhash.h>
#include "ub_mad.h"
#include "ub_cm.h"
#include "ubcm_log.h"
#include "ubcm_genl.h"

struct ubcm_uvs_list {
	spinlock_t lock; /* for both uvs list and eid_hlist of uvs_node */
	struct list_head list; /* uvs genl nodes list */
	int count; /* number of uvs genl nodes in list */
	uint32_t next_id; /* next id for uvs */
};

static struct ubcm_uvs_list g_ubcm_uvs_list = { 0 };
static inline struct ubcm_uvs_list *get_uvs_list(void)
{
	return &g_ubcm_uvs_list;
}
atomic_t g_ubcm_nlmsg_seq;

static int ubcm_genl_uvs_add_handler(struct sk_buff *skb,
				     struct genl_info *info);
static int ubcm_genl_uvs_remove_handler(struct sk_buff *skb,
					struct genl_info *info);
static int ubcm_genl_uvs_add_eid_handler(struct sk_buff *skb,
					 struct genl_info *info);
static int ubcm_genl_uvs_del_eid_handler(struct sk_buff *skb,
					 struct genl_info *info);
static int ubcm_genl_uvs_msg_handler(struct sk_buff *skb,
				     struct genl_info *info);
static int ubcm_genl_uvs_authn_handler(struct sk_buff *skb,
				       struct genl_info *info);

static int ubcm_nl_notifier_call(struct notifier_block *nb,
				 unsigned long action, void *data);

static const struct nla_policy g_ubcm_policy[NUM_UBCM_ATTR] = {
	[UBCM_ATTR_UNSPEC] = { 0 },
	[UBCM_HDR_COMMAND] = { .type = NLA_U32 },
	[UBCM_HDR_ARGS_LEN] = { .type = NLA_U32 },
	[UBCM_HDR_ARGS_ADDR] = { .type = NLA_U64 },
	[UBCM_ATTR_NS_MODE] = { .type = NLA_U8 },
	[UBCM_ATTR_DEV_NAME] = { .type = NLA_STRING,
				 .len = UBCORE_MAX_DEV_NAME - 1 },
	[UBCM_ATTR_NS_FD] = { .type = NLA_U32 },
	[UBCM_MSG_SEQ] = { .type = NLA_U32 },
	[UBCM_MSG_TYPE] = { .type = NLA_U32 },
	[UBCM_SRC_ID] = { .len = UBCORE_EID_SIZE },
	[UBCM_DST_ID] = { .len = UBCORE_EID_SIZE },
	[UBCM_RESERVED] = { .type = NLA_U32 },
	[UBCM_PAYLOAD_DATA] = { .type = NLA_BINARY }
};

static const struct genl_ops g_ubcm_genl_ops[] = {
	{ .cmd = UBCM_CMD_UVS_ADD,
	  .validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,

	  .doit = ubcm_genl_uvs_add_handler },
	{ .cmd = UBCM_CMD_UVS_REMOVE,
	  .validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,

	  .doit = ubcm_genl_uvs_remove_handler },
	{ .cmd = UBCM_CMD_UVS_ADD_EID,
	  .validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,

	  .doit = ubcm_genl_uvs_add_eid_handler },
	{ .cmd = UBCM_CMD_UVS_DEL_EID,
	  .validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,

	  .doit = ubcm_genl_uvs_del_eid_handler },
	{ .cmd = UBCM_CMD_UVS_MSG,
	  .validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,

	  .doit = ubcm_genl_uvs_msg_handler },
	{ .cmd = UBCM_CMD_UVS_AUTHN,
	  .validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,

	  .doit = ubcm_genl_uvs_authn_handler }
};

struct genl_family g_ubcm_genl_family __ro_after_init = {
	.hdrsize = 0,
	.name = UBCM_GENL_FAMILY_NAME,
	.version = UBCM_GENL_FAMILY_VERSION,
	.maxattr = UBCM_ATTR_MAX,
	.policy = g_ubcm_policy,

	.resv_start_op = UBCM_CMD_NUM,

	.netnsok = true,
	.module = THIS_MODULE,
	.ops = g_ubcm_genl_ops,
	.n_ops = ARRAY_SIZE(g_ubcm_genl_ops)
};

static struct notifier_block g_ubcm_nl_notifier = {
	.notifier_call = ubcm_nl_notifier_call,
};

static int ubcm_check_uvs_para(struct genl_info *info, uint32_t *length)
{
	uint32_t payload_len;

	if (!info->attrs[UBCM_PAYLOAD_DATA]) {
		ubcm_log_err("Invalid parameter.\n");
		return -EINVAL;
	}

	payload_len = (uint32_t)nla_len(info->attrs[UBCM_PAYLOAD_DATA]);
	if (payload_len == 0 || payload_len > UBCM_MAX_UVS_NAME_LEN) {
		ubcm_log_err("Invalid payload length: %u.\n", payload_len);
		return -EINVAL;
	}

	*length = payload_len;

	return 0;
}

static int ubcm_copy_uvs_name(struct genl_info *info, char *uvs_name,
			      uint32_t payload_len)
{
	(void)memcpy(uvs_name, nla_data(info->attrs[UBCM_PAYLOAD_DATA]),
		     payload_len);
	uvs_name[UBCM_MAX_UVS_NAME_LEN - 1] = '\0';

	return 0;
}

static struct ubcm_uvs_genl_node *
ubcm_lookup_genl_node_lockless(const char *uvs_name,
			       struct ubcm_uvs_list *uvs_list)
{
	struct ubcm_uvs_genl_node *node, *next;

	list_for_each_entry_safe(node, next, &uvs_list->list, list_node) {
		if (strcmp(node->name, uvs_name) == 0)
			return node;
	}
	return NULL;
}

static int ubcm_genl_uvs_add(const char *uvs_name, uint32_t genl_port,
			     struct sock *genl_sock)
{
	struct ubcm_uvs_list *uvs_list = get_uvs_list();
	struct ubcm_uvs_genl_node *new_node = NULL;
	struct ubcm_uvs_genl_node *node;
	int idx;

	new_node = kzalloc(sizeof(struct ubcm_uvs_genl_node), GFP_ATOMIC);
	if (new_node == NULL)
		return -ENOMEM;

	spin_lock(&uvs_list->lock);
	node = ubcm_lookup_genl_node_lockless(uvs_name, uvs_list);
	if (node != NULL) {
		spin_unlock(&uvs_list->lock);
		ubcm_log_warn("Uvs: %s already exist.\n", uvs_name);
		kfree(new_node);
		return -EEXIST;
	}

	(void)strscpy(new_node->name, uvs_name, UBCM_MAX_UVS_NAME_LEN);
	kref_init(&new_node->ref);
	new_node->pid = (uint32_t)task_tgid_vnr(current);
	new_node->id = uvs_list->next_id;
	new_node->state = UBCM_UVS_STATE_ALIVE;
	atomic_set(&new_node->map2ue, 0);
	new_node->genl_port = genl_port;
	new_node->genl_sock = genl_sock;
	for (idx = 0; idx < UBCM_EID_TABLE_SIZE; idx++)
		INIT_HLIST_HEAD(&new_node->eid_hlist[idx]);

	list_add_tail(&new_node->list_node, &uvs_list->list);
	uvs_list->count++;
	uvs_list->next_id++;
	spin_unlock(&uvs_list->lock);

	ubcm_log_info("Finish to add uvs node: %s, id: %u.\n", uvs_name,
		      new_node->id);
	return 0;
}

static int ubcm_genl_uvs_add_handler(struct sk_buff *skb,
				     struct genl_info *info)
{
	char uvs_name[UBCM_MAX_UVS_NAME_LEN] = { 0 };
	uint32_t payload_len;
	int ret;

	ret = ubcm_check_uvs_para(info, &payload_len);
	if (ret != 0) {
		ubcm_log_err("Invalid add parameter.\n");
		return ret;
	}

	ret = ubcm_copy_uvs_name(info, uvs_name, payload_len);
	if (ret != 0) {
		ubcm_log_err("Failed to copy uvs name.\n");
		return ret;
	}
	ret = ubcm_genl_uvs_add(uvs_name, info->snd_portid,
				genl_info_net(info)->genl_sock);
	if (ret != 0) {
		ubcm_log_err("Failed to add uvs genl node: %s.\n", uvs_name);
		return ret;
	}

	return 0;
}

void ubcm_uvs_kref_get(struct ubcm_uvs_genl_node *node)
{
	kref_get(&node->ref);
}

static void ubcm_uvs_kref_release(struct kref *ref)
{
	struct ubcm_uvs_genl_node *node =
		container_of(ref, struct ubcm_uvs_genl_node, ref);
	struct ubcm_uvs_list *uvs_list = get_uvs_list();
	struct ubcm_uvs_eid_node *eid_node;
	struct hlist_node *next;
	int i;

	spin_lock(&uvs_list->lock);
	for (i = 0; i < UBCM_EID_TABLE_SIZE; i++) {
		hlist_for_each_entry_safe(eid_node, next, &node->eid_hlist[i],
					  node) {
			hlist_del(&eid_node->node);
			kfree(eid_node);
		}
	}
	spin_unlock(&uvs_list->lock);

	ubcm_log_info("Release uvs: %s, uvs_id: %u.\n", node->name, node->id);
	kfree(node);
}

void ubcm_uvs_kref_put(struct ubcm_uvs_genl_node *node)
{
	uint32_t refcnt;

	refcnt = kref_read(&node->ref);
	ubcm_log_info("kref_put: uvs %s, id %u, old refcnt %u, new refcnt %u\n",
		      node->name, node->id, refcnt,
		      refcnt > 0 ? refcnt - 1 : 0);

	(void)kref_put(&node->ref, ubcm_uvs_kref_release);
}

static int ubcm_genl_uvs_remove(const char *uvs_name)
{
	struct ubcm_uvs_list *uvs_list = get_uvs_list();
	struct ubcm_uvs_genl_node *node;

	spin_lock(&uvs_list->lock);
	node = ubcm_lookup_genl_node_lockless(uvs_name, uvs_list);
	if (node == NULL) {
		spin_unlock(&uvs_list->lock);
		ubcm_log_err("Failed to lookup uvs node: %s.\n", uvs_name);
		return -ENOENT;
	}

	if (node->state == UBCM_UVS_STATE_DEAD) {
		spin_unlock(&uvs_list->lock);
		ubcm_log_warn("Uvs: %s already set dead.\n", uvs_name);
		return -EPERM;
	}

	if (atomic_read(&node->map2ue) != 0) {
		node->state = UBCM_UVS_STATE_DEAD;
		spin_unlock(&uvs_list->lock);
		ubcm_log_info(
			"Uvs %s was referenced by ue, set dead and keep it.\n",
			uvs_name);
		return 0;
	}

	list_del(&node->list_node);
	node->state = UBCM_UVS_STATE_DEAD;
	uvs_list->count--;
	spin_unlock(&uvs_list->lock);
	ubcm_uvs_kref_put(node);

	ubcm_log_info("Uvs: %s removed.\n", uvs_name);
	return 0;
}

static int ubcm_genl_uvs_remove_handler(struct sk_buff *skb,
					struct genl_info *info)
{
	char uvs_name[UBCM_MAX_UVS_NAME_LEN];
	uint32_t payload_len;
	int ret;

	ret = ubcm_check_uvs_para(info, &payload_len);
	if (ret != 0) {
		ubcm_log_err("Invalid remove parameter.\n");
		return ret;
	}

	ret = ubcm_copy_uvs_name(info, uvs_name, payload_len);
	if (ret != 0)
		return ret;

	ret = ubcm_genl_uvs_remove(uvs_name);
	if (ret != 0) {
		ubcm_log_err("Failed to remove uvs genl node: %s.\n", uvs_name);
		return ret;
	}

	return 0;
}

static int ubcm_parse_uvs_eid_para(struct genl_info *info,
				   struct ubcm_nlmsg_op_eid *para,
				   enum ubcm_genl_msg_type type)
{
	uint32_t payload_len;
	uint32_t msg_type;

	if (!info->attrs[UBCM_PAYLOAD_DATA]) {
		ubcm_log_err("Invalid parameter.\n");
		return -EINVAL;
	}

	payload_len = (uint32_t)nla_len(info->attrs[UBCM_PAYLOAD_DATA]);
	if (payload_len != sizeof(struct ubcm_nlmsg_op_eid)) {
		ubcm_log_err("Invalid payload length: %u.\n", payload_len);
		return -EINVAL;
	}

	msg_type = nla_get_u32(info->attrs[UBCM_MSG_TYPE]);
	if (msg_type != (uint32_t)type) {
		ubcm_log_err("Invalid msg_type: %u, type: %u.\n", msg_type,
			     (uint32_t)type);
		return -EINVAL;
	}

	(void)memcpy(para, nla_data(info->attrs[UBCM_PAYLOAD_DATA]),
		     payload_len);
	para->uvs_name[UBCM_MAX_UVS_NAME_LEN - 1] = '\0';
	return 0;
}

static struct ubcm_uvs_eid_node *
ubcm_find_eid_node_lockless(struct ubcm_uvs_genl_node *uvs, uint32_t hash,
			    union ubcore_eid *eid)
{
	/* No need to check hash as it is no larger than UBCM_EID_TABLE_SIZE */
	struct ubcm_uvs_eid_node *eid_node;
	struct hlist_node *next;

	hlist_for_each_entry_safe(eid_node, next, &uvs->eid_hlist[hash], node) {
		if (memcmp(&eid_node->eid, eid, sizeof(union ubcore_eid)) == 0)
			return eid_node;
	}

	ubcm_log_info("Failed to lookup eid node: " EID_FMT ", hash: %u.\n",
		      EID_ARGS(*eid), hash);
	return NULL;
}

static int ubcm_add_uvs_eid(struct ubcm_nlmsg_op_eid *para)
{
	uint32_t hash = jhash(&para->eid, sizeof(union ubcore_eid), 0) %
			UBCM_EID_TABLE_SIZE;
	struct ubcm_uvs_list *uvs_list = get_uvs_list();
	struct ubcm_uvs_eid_node *node, *new;
	struct ubcm_uvs_genl_node *uvs;

	/* Step 1: Lookup eid node to judge whether to create new node */
	spin_lock(&uvs_list->lock);
	uvs = ubcm_lookup_genl_node_lockless(para->uvs_name, uvs_list);
	if (uvs == NULL) {
		spin_unlock(&uvs_list->lock);
		ubcm_log_err("Failed to find uvs: %s.\n", para->uvs_name);
		return -EINVAL;
	}

	if (uvs->eid_cnt >= UBCM_EID_TABLE_SIZE) {
		spin_unlock(&uvs_list->lock);
		ubcm_log_err("Invalid operation, eid_cnt: %u.\n", uvs->eid_cnt);
		return -EINVAL;
	}

	node = ubcm_find_eid_node_lockless(uvs, hash, &para->eid);
	if (node != NULL) {
		spin_unlock(&uvs_list->lock);
		ubcm_log_warn("Eid: " EID_FMT " already added in uvs: %s.\n",
			      EID_ARGS(para->eid), para->uvs_name);
		return -1;
	}
	spin_unlock(&uvs_list->lock);

	/* Step 2: Create new eid node */
	new = kzalloc(sizeof(struct ubcm_uvs_genl_node), GFP_KERNEL);
	if (new == NULL)
		return -ENOMEM;
	new->eid_idx = para->eid_idx;
	new->eid = para->eid;
	INIT_HLIST_NODE(&new->node);

	/* Step 3: Lookup eid node to judge whether to add the new node into hlist */
	spin_lock(&uvs_list->lock);
	node = ubcm_find_eid_node_lockless(uvs, hash, &para->eid);
	if (node != NULL) {
		spin_unlock(&uvs_list->lock);
		ubcm_log_warn("Eid: " EID_FMT " added in uvs: %s.\n",
			      EID_ARGS(para->eid), para->uvs_name);
		kfree(new);
		return -1;
	}
	hlist_add_head(&new->node, &uvs->eid_hlist[hash]);
	uvs->eid_cnt++;
	spin_unlock(&uvs_list->lock);
	ubcm_log_info("Finish to add uvs eid: " EID_FMT ", uvs_name: %s.\n",
		      EID_ARGS(para->eid), para->uvs_name);

	return 0;
}

static int ubcm_genl_uvs_add_eid_handler(struct sk_buff *skb,
					 struct genl_info *info)
{
	struct ubcm_nlmsg_op_eid para = { 0 };
	int ret;

	ret = ubcm_parse_uvs_eid_para(info, &para, UBCM_CMD_UVS_ADD_EID);
	if (ret != 0)
		return ret;

	return ubcm_add_uvs_eid(&para);
}

static int ubcm_find_del_eid_node_lockless(struct ubcm_uvs_genl_node *uvs,
					   union ubcore_eid *eid)
{
	uint32_t hash =
		jhash(eid, sizeof(union ubcore_eid), 0) % UBCM_EID_TABLE_SIZE;
	struct ubcm_uvs_eid_node *eid_node;
	struct hlist_node *next;

	hlist_for_each_entry_safe(eid_node, next, &uvs->eid_hlist[hash], node) {
		if (memcmp(&eid_node->eid, eid, sizeof(union ubcore_eid)) ==
		    0) {
			hlist_del(&eid_node->node);
			kfree(eid_node);
			uvs->eid_cnt--;
			return 0;
		}
	}

	ubcm_log_err("Failed to lookup eid node: " EID_FMT ", hash: %u.\n",
		     EID_ARGS(*eid), hash);
	return -1;
}

static int ubcm_del_uvs_eid(struct ubcm_nlmsg_op_eid *para)
{
	struct ubcm_uvs_list *uvs_list = get_uvs_list();
	struct ubcm_uvs_genl_node *uvs;
	int ret;

	spin_lock(&uvs_list->lock);
	uvs = ubcm_lookup_genl_node_lockless(para->uvs_name, uvs_list);
	if (uvs == NULL) {
		spin_unlock(&uvs_list->lock);
		ubcm_log_err("Failed find uvs: %s.\n", para->uvs_name);
		return -EINVAL;
	}
	if (uvs->eid_cnt == 0) {
		spin_unlock(&uvs_list->lock);
		ubcm_log_err("Invalid operation, there is no valid eid.\n");
		return -EINVAL;
	}

	ret = ubcm_find_del_eid_node_lockless(uvs, &para->eid);
	spin_unlock(&uvs_list->lock);

	if (ret != 0) {
		ubcm_log_err("Failed to delete uvs eid: " EID_FMT
			     ", uvs_name: %s.\n",
			     EID_ARGS(para->eid), para->uvs_name);
	} else {
		ubcm_log_info("Finish to delete uvs eid: " EID_FMT
			      ", uvs_name: %s.\n",
			      EID_ARGS(para->eid), para->uvs_name);
	}
	return ret;
}

static int ubcm_genl_uvs_del_eid_handler(struct sk_buff *skb,
					 struct genl_info *info)
{
	struct ubcm_nlmsg_op_eid para = { 0 };
	int ret;

	ret = ubcm_parse_uvs_eid_para(info, &para, UBCM_CMD_UVS_DEL_EID);
	if (ret != 0)
		return ret;

	return ubcm_del_uvs_eid(&para);
}

static struct ubmad_send_buf *ubcm_get_nlmsg_send_buf(struct genl_info *info)
{
	struct ubmad_send_buf *send_buf;
	uint32_t payload_len;

	if (!info->attrs[UBCM_PAYLOAD_DATA]) {
		ubcm_log_err("Invalid parameter.\n");
		return NULL;
	}

	payload_len = (uint32_t)nla_len(info->attrs[UBCM_PAYLOAD_DATA]);
	if (payload_len > UBCM_MAX_NL_MSG_BUF_LEN) {
		ubcm_log_err("Invalid payload_len: %u.\n", payload_len);
		return NULL;
	}

	send_buf =
		kzalloc((size_t)(sizeof(struct ubmad_send_buf) + payload_len),
			GFP_KERNEL);
	if (send_buf == NULL)
		return NULL;

	send_buf->payload_len = payload_len;
	send_buf->msg_type = UBMAD_CONN_DATA; // using wk_jetty0

	if (info->attrs[UBCM_SRC_ID])
		(void)memcpy(&send_buf->src_eid,
			     nla_data(info->attrs[UBCM_SRC_ID]),
			     UBCORE_EID_SIZE);

	if (info->attrs[UBCM_DST_ID])
		(void)memcpy(&send_buf->dst_eid,
			     nla_data(info->attrs[UBCM_DST_ID]),
			     UBCORE_EID_SIZE);

	if (info->attrs[UBCM_PAYLOAD_DATA])
		(void)memcpy(send_buf->payload,
			     nla_data(info->attrs[UBCM_PAYLOAD_DATA]),
			     payload_len);

	return send_buf;
}

static int ubcm_genl_uvs_msg_handler(struct sk_buff *skb,
				     struct genl_info *info)
{
	struct ubcm_context *cm_ctx = get_ubcm_ctx();
	struct ubmad_send_buf *send_buf;
	struct ubcm_work *cm_work;
	bool ret;

	send_buf = ubcm_get_nlmsg_send_buf(info);
	if (send_buf == NULL) {
		ubcm_log_err("Failed to get nlmsg send buffer.\n");
		return -1;
	}

	cm_work = kzalloc(sizeof(struct ubcm_work), GFP_ATOMIC);
	if (cm_work == NULL) {
		kfree(send_buf);
		return -ENOMEM;
	}
	cm_work->send_buf = send_buf;

	INIT_WORK(&cm_work->work, ubcm_work_handler);
	/* return value: 1-work is executing in work-queue; 0-work is not executing */
	ret = queue_work(cm_ctx->wq, &cm_work->work);
	if (!ret) {
		kfree(cm_work);
		kfree(send_buf);
		ubcm_log_err("Cm work already in workqueue, ret: %u.\n", ret);
		return -1;
	}

	return 0;
}

static struct ubmad_send_buf *ubcm_get_nlmsg_authn_buf(struct genl_info *info)
{
	struct ubmad_send_buf *send_buf;

	send_buf = kzalloc((size_t)(sizeof(struct ubmad_send_buf)), GFP_KERNEL);
	if (send_buf == NULL)
		return NULL;
	send_buf->payload_len = 0;
	send_buf->msg_type = UBMAD_AUTHN_DATA; // using wk_jetty1

	if (info->attrs[UBCM_SRC_ID])
		(void)memcpy(&send_buf->src_eid,
			     nla_data(info->attrs[UBCM_SRC_ID]),
			     UBCORE_EID_SIZE);

	if (info->attrs[UBCM_DST_ID])
		(void)memcpy(&send_buf->dst_eid,
			     nla_data(info->attrs[UBCM_DST_ID]),
			     UBCORE_EID_SIZE);

	return send_buf;
}

static int ubcm_genl_uvs_authn_handler(struct sk_buff *skb,
				       struct genl_info *info)
{
	struct ubcm_context *cm_ctx = get_ubcm_ctx();
	struct ubmad_send_buf *send_buf;
	struct ubcm_work *cm_work;
	bool ret;

	send_buf = ubcm_get_nlmsg_authn_buf(info);
	if (send_buf == NULL) {
		ubcm_log_err("Failed to get nlmsg authentication buffer.\n");
		return -1;
	}

	cm_work = kzalloc(sizeof(struct ubcm_work), GFP_ATOMIC);
	if (cm_work == NULL) {
		kfree(send_buf);
		return -ENOMEM;
	}
	cm_work->send_buf = send_buf;

	INIT_WORK(&cm_work->work, ubcm_work_handler);
	/* return value: 1-work is executing in work-queue; 0-work is not executing */
	ret = queue_work(cm_ctx->wq, &cm_work->work);
	if (!ret) {
		kfree(cm_work);
		kfree(send_buf);
		ubcm_log_err("Cm work already in workqueue, ret: %u.\n", ret);
		return -1;
	}

	return 0;
}

static struct ubcm_uvs_genl_node *
ubcm_lookup_node_by_portid_lockless(struct ubcm_uvs_list *uvs_list,
				    uint32_t portid)
{
	struct ubcm_uvs_genl_node *result = NULL;
	struct ubcm_uvs_genl_node *node, *next;

	list_for_each_entry_safe(node, next, &uvs_list->list, list_node) {
		if (node->genl_port == portid) {
			result = node;
			break;
		}
	}

	return result;
}

static void ubcm_unset_genl_pid(uint32_t portid)
{
	struct ubcm_uvs_list *uvs_list = get_uvs_list();
	struct ubcm_uvs_genl_node *node;

	spin_lock(&uvs_list->lock);
	node = ubcm_lookup_node_by_portid_lockless(uvs_list, portid);
	if (node == NULL) {
		spin_unlock(&uvs_list->lock);
		return;
	}

	list_del(&node->list_node);
	spin_unlock(&uvs_list->lock);

	ubcm_log_err("Finish to unset port: %u for uvs: %s, id: %u.\n", portid,
		     node->name, node->id);
	node->genl_port = UBCM_GENL_INVALID_PORT;
	node->genl_sock = NULL;
	/* free node buffer */
	ubcm_uvs_kref_put(node);
}

static int ubcm_nl_notifier_call(struct notifier_block *nb,
				 unsigned long action, void *data)
{
	struct netlink_notify *notify = data;

	if (action != NETLINK_URELEASE || notify == NULL ||
	    notify->protocol != NETLINK_GENERIC)
		return NOTIFY_DONE;

	ubcm_unset_genl_pid(notify->portid);
	return NOTIFY_DONE;
}

static void ubcm_uvs_list_init(void)
{
	struct ubcm_uvs_list *uvs_list = get_uvs_list();

	spin_lock_init(&uvs_list->lock);
	INIT_LIST_HEAD(&uvs_list->list);
	uvs_list->count = 0;
	uvs_list->next_id = 1; /* 0 for invalid uvs id */
}

static void ubcm_uvs_list_uninit(void)
{
	struct ubcm_uvs_list *uvs_list = get_uvs_list();
	struct ubcm_uvs_genl_node *node, *next;

	spin_lock(&uvs_list->lock);
	list_for_each_entry_safe(node, next, &uvs_list->list, list_node) {
		list_del(&node->list_node);
		kfree(node);
	}
	uvs_list->count = 0;
	uvs_list->next_id = 0;
	spin_unlock(&uvs_list->lock);
}

int ubcm_genl_init(void)
{
	int ret;

	ubcm_uvs_list_init();
	ret = genl_register_family(&g_ubcm_genl_family);
	if (ret != 0)
		ubcm_log_err(
			"Failed to init ubcm generic netlink family, ret: %d.\n",
			ret);

	ret = netlink_register_notifier(&g_ubcm_nl_notifier);
	if (ret != 0)
		ubcm_log_err("Failed to register notifier, ret: %d.\n", ret);

	ubcm_log_info("Finish to init ubcm generic netlink.\n");
	return ret;
}

void ubcm_genl_uninit(void)
{
	(void)netlink_unregister_notifier(&g_ubcm_nl_notifier);
	(void)genl_unregister_family(&g_ubcm_genl_family);
	ubcm_uvs_list_uninit();
}

struct ubcm_nlmsg *ubcm_alloc_genl_msg(struct ubmad_recv_cr *recv_cr)
{
	uint32_t payload_len = recv_cr->payload_len;
	struct ubcm_nlmsg *nlmsg;

	nlmsg = kzalloc(sizeof(struct ubcm_nlmsg) + payload_len, GFP_KERNEL);
	if (nlmsg == NULL)
		return NULL;

	nlmsg->src_eid = recv_cr->cr->remote_id.eid;
	nlmsg->dst_eid = recv_cr->local_eid;
	nlmsg->msg_type = UBCM_CMD_UVS_MSG;
	nlmsg->payload_len = payload_len;
	(void)memcpy(nlmsg->payload, (const void *)recv_cr->payload,
		     payload_len);
	nlmsg->nlmsg_seq = ubcm_get_nlmsg_seq();

	return nlmsg;
}

struct ubcm_nlmsg *ubcm_alloc_genl_authn_msg(struct ubmad_recv_cr *recv_cr)
{
	uint32_t payload_len = recv_cr->payload_len;
	struct ubcm_nlmsg *nlmsg;

	if (payload_len != 0) {
		ubcm_log_err("Invalid payload length: %u.\n", payload_len);
		return ERR_PTR(-EINVAL);
	}
	nlmsg = kzalloc(sizeof(struct ubcm_nlmsg), GFP_KERNEL);
	if (nlmsg == NULL)
		return NULL;

	nlmsg->src_eid = recv_cr->cr->remote_id.eid;
	nlmsg->dst_eid = recv_cr->local_eid;
	nlmsg->msg_type = UBCM_CMD_UVS_AUTHN;
	nlmsg->payload_len = payload_len;
	nlmsg->nlmsg_seq = ubcm_get_nlmsg_seq();

	return nlmsg;
}

struct ubcm_uvs_genl_node *ubcm_find_get_uvs_by_eid(union ubcore_eid *eid)
{
	uint32_t hash =
		jhash(eid, sizeof(union ubcore_eid), 0) % UBCM_EID_TABLE_SIZE;
	struct ubcm_uvs_list *uvs_list = get_uvs_list();
	struct ubcm_uvs_genl_node *uvs, *next_uvs;
	struct ubcm_uvs_eid_node *node;
	struct hlist_node *next_node;

	spin_lock(&uvs_list->lock);
	list_for_each_entry_safe(uvs, next_uvs, &uvs_list->list, list_node) {
		if (IS_ERR_OR_NULL(uvs) || uvs->eid_cnt == 0)
			continue;
		hlist_for_each_entry_safe(node, next_node,
					  &uvs->eid_hlist[hash], node) {
			if (memcmp(&node->eid, eid, sizeof(union ubcore_eid)) ==
			    0) {
				ubcm_uvs_kref_get(uvs);
				spin_unlock(&uvs_list->lock);
				ubcm_log_info("Find uvs: %s by eid: " EID_FMT
					      ".\n",
					      uvs->name, EID_ARGS(*eid));
				return uvs;
			}
		}
	}
	spin_unlock(&uvs_list->lock);
	ubcm_log_err("Failed to find uvs by eid: " EID_FMT ".\n",
		     EID_ARGS(*eid));

	return NULL;
}

int ubcm_genl_unicast(struct ubcm_nlmsg *msg, uint32_t len,
		      struct ubcm_uvs_genl_node *uvs)
{
	struct sk_buff *nl_skb;
	struct nlmsghdr *nlh;
	int ret;

	if (msg == NULL || uvs->genl_sock == NULL ||
	    uvs->genl_port == UBCM_GENL_INVALID_PORT) {
		ubcm_log_err("Invalid parameter.\n");
		return -EINVAL;
	}

	/* create sk_buff */
	nl_skb = genlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (nl_skb == NULL) {
		ubcm_log_err("Failed to creatge nl_skb.\n");
		return -1;
	}

	/* set genl head */
	nlh = genlmsg_put(nl_skb, uvs->genl_port, msg->nlmsg_seq,
			  &g_ubcm_genl_family, NLM_F_ACK,
			  (uint8_t)msg->msg_type);
	if (nlh == NULL) {
		ubcm_log_err("Failed to nlmsg put.\n");
		nlmsg_free(nl_skb);
		return -1;
	}
	if (nla_put_u32(nl_skb, UBCM_MSG_SEQ, msg->nlmsg_seq) ||
	    nla_put_u32(nl_skb, UBCM_MSG_TYPE, msg->msg_type) ||
	    nla_put(nl_skb, UBCM_SRC_ID, (int)sizeof(union ubcore_eid),
		    &msg->src_eid) ||
	    nla_put(nl_skb, UBCM_DST_ID, (int)sizeof(union ubcore_eid),
		    &msg->dst_eid) ||
	    nla_put(nl_skb, UBCM_PAYLOAD_DATA, (int)msg->payload_len,
		    msg->payload)) {
		ubcm_log_err("Failed in nla_put operations.\n");
		nlmsg_free(nl_skb);
		return -1;
	}

	genlmsg_end(nl_skb, nlh);
	ubcm_log_info("Finish to send genl msg, seq: %u, payload_len: %u.\n",
		      msg->nlmsg_seq, msg->payload_len);

	ret = nlmsg_unicast(uvs->genl_sock, nl_skb, uvs->genl_port);
	if (ret != 0) {
		ubcm_log_err("Failed to send genl msg, ret: %d.\n", ret);
		nlmsg_free(nl_skb);
		return ret;
	}

	return 0;
}

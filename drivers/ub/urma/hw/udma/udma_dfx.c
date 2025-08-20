// SPDX-License-Identifier: GPL-2.0+
/* Copyright(c) 2025 HiSilicon Technologies CO., Ltd. All rights reserved. */

#define dev_fmt(fmt) "UDMA: " fmt

#include "udma_cmd.h"
#include "udma_jfr.h"
#include "udma_jfs.h"
#include "udma_jfc.h"
#include "udma_jetty.h"
#include "udma_rct.h"
#include "udma_dfx.h"

bool dfx_switch;

static int udma_query_res_list(struct udma_dev *udma_dev,
			       struct udma_dfx_entity *entity,
			       struct ubcore_res_val *val,
			       const char *name)
{
	struct ubcore_res_list_val *res_list = (struct ubcore_res_list_val *)val->addr;
	size_t idx = 0;
	uint32_t *id;

	res_list->cnt = 0;

	read_lock(&entity->rwlock);
	if (entity->cnt == 0) {
		read_unlock(&entity->rwlock);
		return 0;
	}

	res_list->list = vmalloc(sizeof(uint32_t) * entity->cnt);
	if (!res_list->list) {
		read_unlock(&entity->rwlock);
		dev_err(udma_dev->dev, "failed to vmalloc %s_list, %s_cnt = %u!\n",
			name, name, entity->cnt);
		return -ENOMEM;
	}

	xa_for_each(&entity->table, idx, id) {
		if (res_list->cnt >= entity->cnt) {
			read_unlock(&entity->rwlock);
			vfree(res_list->list);
			dev_err(udma_dev->dev,
				"failed to query %s_id, %s_cnt = %u!\n",
				name, name, entity->cnt);
			return -EINVAL;
		}
		res_list->list[res_list->cnt] = idx;
		++res_list->cnt;
	}
	read_unlock(&entity->rwlock);

	return 0;
}

static int udma_query_res_dev_seg(struct udma_dev *udma_dev,
				  struct ubcore_res_val *val)
{
	struct ubcore_res_seg_val *res_list = (struct ubcore_res_seg_val *)val->addr;
	struct ubcore_seg_info *seg_list;
	struct udma_dfx_seg *seg = NULL;
	size_t idx;

	res_list->seg_cnt = 0;

	read_lock(&udma_dev->dfx_info->seg.rwlock);
	if (udma_dev->dfx_info->seg.cnt == 0) {
		read_unlock(&udma_dev->dfx_info->seg.rwlock);
		return 0;
	}

	seg_list = vmalloc(sizeof(*seg_list) * udma_dev->dfx_info->seg.cnt);
	if (!seg_list) {
		read_unlock(&udma_dev->dfx_info->seg.rwlock);
		return -ENOMEM;
	}

	xa_for_each(&udma_dev->dfx_info->seg.table, idx, seg) {
		if (res_list->seg_cnt >= udma_dev->dfx_info->seg.cnt) {
			read_unlock(&udma_dev->dfx_info->seg.rwlock);
			vfree(seg_list);
			dev_err(udma_dev->dev,
				"failed to query seg_list, seg_cnt = %u!\n",
				udma_dev->dfx_info->seg.cnt);
			return -EINVAL;
		}
		seg_list[res_list->seg_cnt].token_id = seg->id;
		seg_list[res_list->seg_cnt].len = seg->len;
		seg_list[res_list->seg_cnt].ubva = seg->ubva;
		seg_list[res_list->seg_cnt].ubva.va = 0;
		++res_list->seg_cnt;
	}
	read_unlock(&udma_dev->dfx_info->seg.rwlock);

	res_list->seg_list = seg_list;

	return 0;
}

static int udma_query_res_rc(struct udma_dev *udma_dev,
			     struct ubcore_res_key *key,
			     struct ubcore_res_val *val)
{
	struct ubcore_res_rc_val *res_rc = (struct ubcore_res_rc_val *)val->addr;
	struct ubase_mbx_attr mbox_attr = {};
	struct ubase_cmd_mailbox *mailbox;
	struct udma_rc_ctx *rcc;
	uint32_t *rc_id;

	if (key->key_cnt == 0)
		return udma_query_res_list(udma_dev, &udma_dev->dfx_info->rc, val, "rc");

	rc_id = (uint32_t *)xa_load(&udma_dev->dfx_info->rc.table, key->key);
	if (!rc_id) {
		dev_err(udma_dev->dev, "failed to query rc, rc_id = %u.\n",
			key->key);
		return -EINVAL;
	}
	mbox_attr.tag = key->key;
	mbox_attr.op = UDMA_CMD_QUERY_RC_CONTEXT;
	mailbox = udma_mailbox_query_ctx(udma_dev, &mbox_attr);
	if (!mailbox)
		return -ENOMEM;

	rcc = (struct udma_rc_ctx *)mailbox->buf;
	res_rc->rc_id = key->key;
	res_rc->depth = 1 << rcc->rce_shift;
	res_rc->type = 0;
	res_rc->state = 0;
	rcc->rce_base_addr_l = 0;
	rcc->rce_base_addr_h = 0;

	udma_dfx_ctx_print(udma_dev, "RC", key->key, sizeof(*rcc) / sizeof(uint32_t),
			   (uint32_t *)rcc);
	udma_free_cmd_mailbox(udma_dev, mailbox);

	return 0;
}

static int udma_query_res_seg(struct udma_dev *udma_dev, struct ubcore_res_key *key,
			      struct ubcore_res_val *val)
{
	struct ubcore_res_seg_val *res_seg = (struct ubcore_res_seg_val *)val->addr;
	struct udma_dfx_seg *seg;

	if (key->key_cnt == 0)
		return udma_query_res_dev_seg(udma_dev, val);

	read_lock(&udma_dev->dfx_info->seg.rwlock);
	seg = (struct udma_dfx_seg *)xa_load(&udma_dev->dfx_info->seg.table, key->key);
	if (!seg) {
		read_unlock(&udma_dev->dfx_info->seg.rwlock);
		dev_err(udma_dev->dev, "failed to query seg, token_id = %u.\n",
			key->key);
		return -EINVAL;
	}

	res_seg->seg_list = vmalloc(sizeof(struct ubcore_seg_info));
	if (!res_seg->seg_list) {
		read_unlock(&udma_dev->dfx_info->seg.rwlock);
		return -ENOMEM;
	}

	res_seg->seg_cnt = 1;
	res_seg->seg_list->token_id = seg->id;
	res_seg->seg_list->len = seg->len;
	res_seg->seg_list->ubva = seg->ubva;
	res_seg->seg_list->ubva.va = 0;
	read_unlock(&udma_dev->dfx_info->seg.rwlock);

	return 0;
}

static int udma_query_res_dev_ta(struct udma_dev *udma_dev,
				 struct ubcore_res_key *key,
				 struct ubcore_res_val *val)
{
	struct ubcore_res_dev_ta_val *res_ta = (struct ubcore_res_dev_ta_val *)val->addr;
	struct udma_dfx_info *dfx = udma_dev->dfx_info;
	struct udma_dfx_entity_cnt udma_dfx_entity_cnt_ta[] = {
		{&dfx->rc.rwlock, &dfx->rc, &res_ta->rc_cnt},
		{&dfx->jetty.rwlock, &dfx->jetty, &res_ta->jetty_cnt},
		{&dfx->jetty_grp.rwlock, &dfx->jetty_grp, &res_ta->jetty_group_cnt},
		{&dfx->jfs.rwlock, &dfx->jfs, &res_ta->jfs_cnt},
		{&dfx->jfr.rwlock, &dfx->jfr, &res_ta->jfr_cnt},
		{&dfx->jfc.rwlock, &dfx->jfc, &res_ta->jfc_cnt},
		{&dfx->seg.rwlock, &dfx->seg, &res_ta->seg_cnt},
	};

	int size = ARRAY_SIZE(udma_dfx_entity_cnt_ta);
	int i;

	for (i = 0; i < size; i++) {
		read_lock(udma_dfx_entity_cnt_ta[i].rwlock);
		*udma_dfx_entity_cnt_ta[i].res_cnt =
			udma_dfx_entity_cnt_ta[i].entity->cnt;
		read_unlock(udma_dfx_entity_cnt_ta[i].rwlock);
	}

	return 0;
}

typedef int (*udma_query_res_handler)(struct udma_dev *udma_dev,
				      struct ubcore_res_key *key,
				      struct ubcore_res_val *val);

static udma_query_res_handler g_udma_query_res_handlers[] = {
	[0] = NULL,
	[UBCORE_RES_KEY_RC] = udma_query_res_rc,
	[UBCORE_RES_KEY_SEG] = udma_query_res_seg,
	[UBCORE_RES_KEY_DEV_TA] = udma_query_res_dev_ta,
};

int udma_query_res(struct ubcore_device *dev, struct ubcore_res_key *key,
		   struct ubcore_res_val *val)
{
	struct udma_dev *udma_dev = to_udma_dev(dev);

	if (!dfx_switch) {
		dev_warn(udma_dev->dev, "the dfx_switch is not enabled.\n");
		return -EPERM;
	}

	if (key->type < UBCORE_RES_KEY_VTP || key->type > UBCORE_RES_KEY_DEV_TP ||
	    g_udma_query_res_handlers[key->type] == NULL) {
		dev_err(udma_dev->dev, "key type(%u) invalid.\n", key->type);
		return -EINVAL;
	}

	return g_udma_query_res_handlers[key->type](udma_dev, key, val);
}

static void list_lock_init(struct udma_dfx_info *dfx)
{
	struct udma_dfx_entity_initialization udma_dfx_entity_initialization_arr[] = {
		{&dfx->rc.rwlock, &dfx->rc.table},
		{&dfx->jetty.rwlock, &dfx->jetty.table},
		{&dfx->jetty_grp.rwlock, &dfx->jetty_grp.table},
		{&dfx->jfs.rwlock, &dfx->jfs.table},
		{&dfx->jfr.rwlock, &dfx->jfr.table},
		{&dfx->jfc.rwlock, &dfx->jfc.table},
		{&dfx->seg.rwlock, &dfx->seg.table},
	};
	int size = ARRAY_SIZE(udma_dfx_entity_initialization_arr);
	int i;

	for (i = 0; i < size; i++) {
		rwlock_init(udma_dfx_entity_initialization_arr[i].rwlock);
		xa_init(udma_dfx_entity_initialization_arr[i].table);
	}
}

int udma_dfx_init(struct udma_dev *udma_dev)
{
	if (!dfx_switch)
		return 0;

	udma_dev->dfx_info = kzalloc(sizeof(struct udma_dfx_info), GFP_KERNEL);
	if (!udma_dev->dfx_info)
		return -ENOMEM;

	list_lock_init(udma_dev->dfx_info);

	return 0;
}

static void udma_dfx_destroy_xa(struct udma_dev *udma_dev, struct xarray *table,
				const char *name)
{
	if (!xa_empty(table))
		dev_err(udma_dev->dev, "%s table is not empty.\n", name);
	xa_destroy(table);
}

static void udma_dfx_table_free(struct udma_dev *dev)
{
	udma_dfx_destroy_xa(dev, &dev->dfx_info->rc.table, "rc");
	udma_dfx_destroy_xa(dev, &dev->dfx_info->jetty.table, "jetty");
	udma_dfx_destroy_xa(dev, &dev->dfx_info->jetty_grp.table, "jetty_grp");
	udma_dfx_destroy_xa(dev, &dev->dfx_info->jfs.table, "jfs");
	udma_dfx_destroy_xa(dev, &dev->dfx_info->jfr.table, "jfr");
	udma_dfx_destroy_xa(dev, &dev->dfx_info->jfc.table, "jfc");
	udma_dfx_destroy_xa(dev, &dev->dfx_info->seg.table, "seg");
}

void udma_dfx_uninit(struct udma_dev *udma_dev)
{
	if (!dfx_switch)
		return;

	udma_dfx_table_free(udma_dev);
	kfree(udma_dev->dfx_info);
	udma_dev->dfx_info = NULL;
}

module_param(dfx_switch, bool, 0444);
MODULE_PARM_DESC(dfx_switch, "Set whether to enable the udma_dfx function, default: 0(0:off, 1:on)");

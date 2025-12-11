// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 *
 * Description: ubagg kernel module
 * Author: Dongxu Li
 * Create: 2025-1-14
 * Note:
 * History: 2025-1-14: Create file
 */
#include <linux/module.h>
#include <linux/list.h>
#include <linux/string.h>
#include <linux/kref.h>

#include <ub/urma/ubcore_api.h>
#include <ub/urma/ubcore_uapi.h>
#include <ub/urma/ubcore_types.h>
#include "ubagg_log.h"
#include "ubagg_ioctl.h"
#include "ubagg_jetty.h"
#include "ubagg_seg.h"
#include "ubagg_bitmap.h"
#include "ubagg_hash_table.h"

#define UBAGG_DEVICE_MAX_EID_CNT 128
#define UBAGG_MAX_BONDING_DEV_NUM 256
#define UBAGG_DEV_NAME_PREFIX "bonding_dev_"
#define MAX_NUM_LEN 11
#define BITMAP_OFFSET 1025
#define BASE_DECIMAL 10

static LIST_HEAD(g_ubagg_dev_list);
static DEFINE_SPINLOCK(g_ubagg_dev_list_lock);

struct seg_info_req {
	struct ubcore_ubva ubva;
	uint64_t len;
	uint32_t token_id;
};

struct jetty_info_req {
	struct ubcore_jetty_id jetty_id;
	bool is_jfr;
};

static struct ubagg_ht_param g_ubagg_ht_params[] = {
	[UBAGG_HT_SEGMENT_HT] = { UBAGG_BITMAP_SIZE,
				  sizeof(struct ubagg_seg_hash_node) -
					  sizeof(struct hlist_node),
				  sizeof(struct ubcore_target_seg),
				  sizeof(uint32_t) },
	[UBAGG_HT_JETTY_HT] = { UBAGG_BITMAP_SIZE,
				sizeof(struct ubagg_jetty_hash_node) -
					sizeof(struct hlist_node),
				sizeof(struct ubcore_jetty), sizeof(uint32_t) },
	[UBAGG_HT_JFR_HT] = { UBAGG_BITMAP_SIZE,
			      sizeof(struct ubagg_jfr_hash_node) -
				      sizeof(struct hlist_node),
			      sizeof(struct ubcore_jfr), sizeof(uint32_t) },
};

static void ubagg_dev_release(struct kref *kref)
{
	struct ubagg_device *dev = container_of(kref, struct ubagg_device, ref);

	kfree(dev);
}

void ubagg_dev_ref_get(struct ubagg_device *dev)
{
	kref_get(&dev->ref);
}

void ubagg_dev_ref_put(struct ubagg_device *dev)
{
	kref_put(&dev->ref, ubagg_dev_release);
}

struct ubagg_dev_name_eid_arr {
	char master_dev_name[UBAGG_MAX_DEV_NAME_LEN];
	char bonding_eid[EID_LEN];
};
static struct ubagg_dev_name_eid_arr
	g_name_eid_arr[UBAGG_MAX_BONDING_DEV_NUM] = { 0 };
static DEFINE_MUTEX(g_name_eid_arr_lock);

static bool g_device_id_has_use[UBAGG_MAX_BONDING_DEV_NUM] = { 0 };
static DEFINE_MUTEX(g_device_id_lock);

static int find_bond_device_id(void)
{
	int use_id, i;

	mutex_lock(&g_device_id_lock);
	for (i = 0; i < UBAGG_MAX_BONDING_DEV_NUM; i++) {
		if (g_device_id_has_use[i] == false) {
			use_id = i;
			g_device_id_has_use[i] = true;
			break;
		}
	}
	mutex_unlock(&g_device_id_lock);
	if (i == UBAGG_MAX_BONDING_DEV_NUM) {
		ubagg_log_err("no free device id.\n");
		return -1;
	}
	return use_id;
}

static void release_bond_device_id(int id)
{
	mutex_lock(&g_device_id_lock);
	g_device_id_has_use[id] = false;
	mutex_unlock(&g_device_id_lock);
}

static int release_bond_device_id_with_name(const char *str)
{
	const char *underscore_pos;
	int id;
	int ret;

	if (!str) {
		ubagg_log_err("name str is null\n");
		return -EINVAL;
	}

	underscore_pos = strrchr(str, '_');
	if (!underscore_pos) {
		ubagg_log_err("invalid dev name: %s\n", str);
		return -EINVAL;
	}
	if (underscore_pos[1] == '\0') {
		ubagg_log_err("dev name is invalid\n");
		return -EINVAL;
	}
	ret = kstrtoint(underscore_pos + 1, BASE_DECIMAL, &id);
	if (ret) {
		ubagg_log_err("str to int failed\n");
		return ret;
	}
	release_bond_device_id(id);
	return 0;
}

static char *generate_master_dev_name(void)
{
	char *name = NULL;
	int cur_id;
	int max_length;

	cur_id = find_bond_device_id();
	if (cur_id < 0) {
		ubagg_log_err("no free device id.\n");
		return NULL;
	}

	max_length = strlen(UBAGG_DEV_NAME_PREFIX) + MAX_NUM_LEN;
	name = kmalloc_array(max_length, sizeof(char), GFP_KERNEL);
	if (name == NULL) {
		release_bond_device_id(cur_id);
		ubagg_log_err("malloc master dev name failed.\n");
		return NULL;
	}
	(void)snprintf(name, max_length, "%s%d", UBAGG_DEV_NAME_PREFIX, cur_id);
	return name;
}

static bool ubagg_dev_exists(char *dev_name)
{
	struct ubagg_device *dev;

	list_for_each_entry(dev, &g_ubagg_dev_list, list_node) {
		if (strncmp(dev_name, dev->master_dev_name,
			    UBAGG_MAX_DEV_NAME_LEN) == 0)
			return true;
	}
	return false;
}

static struct ubagg_device *ubagg_find_dev_by_name(char *dev_name)
{
	struct ubagg_device *dev;
	unsigned long flags;

	spin_lock(&g_ubagg_dev_list_lock);
	list_for_each_entry(dev, &g_ubagg_dev_list, list_node) {
		if (strncmp(dev_name, dev->master_dev_name,
			    UBAGG_MAX_DEV_NAME_LEN) == 0) {
			spin_unlock_irqrestore(&g_ubagg_dev_list_lock, flags);
			return dev;
		}
	}
	spin_unlock(&g_ubagg_dev_list_lock);
	return NULL;
}

static struct ubagg_device *
ubagg_find_dev_by_name_and_rmv_from_list(char *dev_name)
{
	struct ubagg_device *dev, *target = NULL;
	unsigned long flags;

	spin_lock_irqsave(&g_ubagg_dev_list_lock, flags);
	list_for_each_entry(dev, &g_ubagg_dev_list, list_node) {
		if (strncmp(dev_name, dev->master_dev_name,
			    UBAGG_MAX_DEV_NAME_LEN) == 0) {
			target = dev;
			list_del(&dev->list_node);
			ubagg_dev_ref_put(dev);
			break;
		}
	}
	spin_unlock_irqrestore(&g_ubagg_dev_list_lock, flags);
	return target;
}

static bool get_slave_dev(char *dev_name, struct ubagg_slave_device *slave_dev)
{
	struct ubagg_device *ubagg_dev = ubagg_find_dev_by_name(dev_name);
	int i;

	if (ubagg_dev == NULL) {
		ubagg_log_err("aggregation device not exist.");
		return false;
	}

	slave_dev->slave_dev_num = ubagg_dev->slave_dev_num;
	for (i = 0; i < ubagg_dev->slave_dev_num; i++)
		(void)memcpy(slave_dev->slave_dev_name[i],
			     ubagg_dev->slave_dev_name[i],
			     UBAGG_MAX_DEV_NAME_LEN);
	return true;
}

static int ubagg_get_slave_device(struct ubcore_device *dev,
				  struct ubcore_user_ctl *user_ctl)
{
	struct ubagg_slave_device slave_dev = { 0 };
	int ret;

	if (!get_slave_dev(dev->dev_name, &slave_dev)) {
		ubagg_log_err("ubagg dev not exist:%s", dev->dev_name);
		return -ENXIO;
	}

	if (user_ctl->out.len < sizeof(struct ubagg_slave_device)) {
		ubagg_log_err(
			"ubagg user ctl has no enough space, buffer size:%u, needed size:%lu",
			user_ctl->out.len, sizeof(struct ubagg_slave_device));
		return -ENOSPC;
	}

	ret = copy_to_user((void __user *)user_ctl->out.addr,
			   (void *)&slave_dev, sizeof(slave_dev));
	if (ret != 0) {
		ubagg_log_err("copy to user fail, ret:%d", ret);
		return -EFAULT;
	}
	return 0;
}

static struct ubagg_topo_info_out *get_topo_info(void)
{
	struct ubagg_topo_info_out *out = NULL;
	struct ubagg_topo_map *topo_map = NULL;

	topo_map = get_global_ubagg_map();
	if (topo_map == NULL)
		return NULL;
	out = kzalloc(sizeof(struct ubagg_topo_info_out), GFP_KERNEL);
	if (out == NULL)
		return NULL;
	(void)memcpy(out->topo_info, topo_map->topo_infos,
		     sizeof(topo_map->topo_infos));
	out->node_num = topo_map->node_num;
	return out;
}

static int ubagg_get_topo_info(struct ubcore_device *dev,
			       struct ubcore_user_ctl *user_ctl)
{
	struct ubagg_topo_info_out *topo_info_out = NULL;
	int ret;

	topo_info_out = get_topo_info();
	if (!topo_info_out) {
		ubagg_log_err("ubagg dev topo info does not exist:%s",
			      dev->dev_name);
		return -ENXIO;
	}

	if (user_ctl->out.len < sizeof(struct ubagg_topo_info_out)) {
		ubagg_log_err(
			"ubagg user ctl has no enough space, buffer size:%u, needed size:%lu",
			user_ctl->out.len, sizeof(struct ubagg_topo_info_out));
		kfree(topo_info_out);
		return -ENOSPC;
	}

	ret = copy_to_user((void __user *)user_ctl->out.addr,
			   (void *)topo_info_out,
			   sizeof(struct ubagg_topo_info_out));
	if (ret != 0) {
		ubagg_log_err("copy to user fail, ret:%d", ret);
		kfree(topo_info_out);
		return -EFAULT;
	}
	kfree(topo_info_out);
	return 0;
}

static int ubagg_get_jfr_id(struct ubcore_device *dev,
			    struct ubcore_user_ctl *user_ctl)
{
	struct ubagg_device *ubagg_dev = to_ubagg_dev(dev);
	uint32_t id;
	int ret;

	if ((ubagg_dev == NULL) || (ubagg_dev->jfr_bitmap == NULL)) {
		ubagg_log_err("ubagg_dev->jfr_bitmap NULL");
		return -1;
	}
	id = ubagg_bitmap_alloc_idx(ubagg_dev->jfr_bitmap);
	ret = copy_to_user((void __user *)user_ctl->out.addr, (void *)&id,
			   sizeof(uint32_t));
	if (ret != 0) {
		ubagg_log_err("copy to user fail, ret:%d", ret);
		return -EFAULT;
	}
	return ret;
}

static int ubagg_get_jetty_id(struct ubcore_device *dev,
			      struct ubcore_user_ctl *user_ctl)
{
	struct ubagg_device *ubagg_dev = to_ubagg_dev(dev);
	uint32_t id;
	int ret;

	if ((ubagg_dev == NULL) || (ubagg_dev->jetty_bitmap == NULL)) {
		ubagg_log_err("ubagg_dev->jfr_bitmap NULL");
		return -EINVAL;
	}
	id = ubagg_bitmap_alloc_idx(ubagg_dev->jetty_bitmap);
	ret = copy_to_user((void __user *)user_ctl->out.addr, (void *)&id,
			   sizeof(uint32_t));
	if (ret != 0) {
		ubagg_log_err("copy to user fail, ret:%d", ret);
		return -EFAULT;
	}
	return ret;
}

static int ubagg_get_seg_info(struct ubcore_device *dev,
			      struct ubcore_user_ctl *user_ctl)
{
	struct ubagg_device *ubagg_dev = to_ubagg_dev(dev);
	struct ubagg_hash_table *ubagg_seg_ht = NULL;
	struct ubagg_seg_hash_node *tmp_seg = NULL;
	struct seg_info_req *req = NULL;

	if ((ubagg_dev == NULL) || (ubagg_dev->segment_bitmap == NULL)) {
		ubagg_log_err("ubagg_dev->segment_bitmap NULL");
		return -1;
	}

	if (user_ctl->in.addr != 0 &&
	    user_ctl->in.len != sizeof(struct seg_info_req)) {
		ubagg_log_err("Invalid user in");
		return -1;
	}
	req = (struct seg_info_req *)user_ctl->in.addr;

	ubagg_seg_ht = &ubagg_dev->ubagg_ht[UBAGG_HT_SEGMENT_HT];
	spin_lock(&ubagg_seg_ht->lock);
	tmp_seg = ubagg_hash_table_lookup_nolock(ubagg_seg_ht, req->token_id,
						 &req->token_id);
	if (tmp_seg == NULL) {
		spin_unlock(&ubagg_seg_ht->lock);
		ubagg_log_err("Failed to find seg.\n");
		return -1;
	}
	spin_unlock(&ubagg_seg_ht->lock);
	memcpy((void *)user_ctl->out.addr, tmp_seg->ex_info.slaves,
	       sizeof(tmp_seg->ex_info.slaves));
	return 0;
}

static int ubagg_get_jetty_info(struct ubcore_device *dev,
				struct ubcore_user_ctl *user_ctl)
{
	struct ubagg_hash_table *ht = NULL;
	struct ubagg_device *ubagg_dev = to_ubagg_dev(dev);
	struct jetty_info_req *req = NULL;

	if ((ubagg_dev == NULL) || (ubagg_dev->segment_bitmap == NULL)) {
		ubagg_log_err("ubagg_dev->segment_bitmap NULL");
		return -1;
	}

	if (user_ctl->in.addr != 0 &&
	    user_ctl->in.len != sizeof(struct jetty_info_req)) {
		ubagg_log_err("Invalid user in");
		return -1;
	}
	req = (struct jetty_info_req *)user_ctl->in.addr;

	if (req->is_jfr) {
		struct ubagg_jfr_hash_node *tmp_jfr = NULL;

		ht = &ubagg_dev->ubagg_ht[UBAGG_HT_JFR_HT];
		spin_lock(&ht->lock);
		tmp_jfr = ubagg_hash_table_lookup_nolock(ht, req->jetty_id.id,
							 &req->jetty_id.id);
		if (tmp_jfr == NULL) {
			spin_unlock(&ht->lock);
			ubagg_log_err("Failed to find jfr, jetty_id:%u.\n",
				      req->jetty_id.id);
			return -1;
		}
		memcpy((void *)user_ctl->out.addr, &tmp_jfr->ex_info,
		       sizeof(tmp_jfr->ex_info));
		spin_unlock(&ht->lock);
	} else {
		struct ubagg_jetty_hash_node *tmp_jetty = NULL;

		ht = &ubagg_dev->ubagg_ht[UBAGG_HT_JETTY_HT];
		spin_lock(&ht->lock);
		tmp_jetty = ubagg_hash_table_lookup_nolock(ht, req->jetty_id.id,
							   &req->jetty_id.id);
		if (tmp_jetty == NULL) {
			spin_unlock(&ht->lock);
			ubagg_log_err("Failed to find jetty, jetty_id:%u.\n",
				      req->jetty_id.id);
			return -1;
		}
		memcpy((void *)user_ctl->out.addr, &tmp_jetty->ex_info,
		       sizeof(tmp_jetty->ex_info));
		spin_unlock(&ht->lock);
	}
	return 0;
}

int ubagg_user_ctl(struct ubcore_device *dev, struct ubcore_user_ctl *user_ctl)
{
	int ret = 0;

	if (dev == NULL || user_ctl == NULL) {
		ubagg_log_err("Invalid parameter.\n");
		return -1;
	}

	switch (user_ctl->in.opcode) {
	case GET_SLAVE_DEVICE:
		ret = ubagg_get_slave_device(dev, user_ctl);
		break;
	case GET_TOPO_INFO:
		ret = ubagg_get_topo_info(dev, user_ctl);
		break;
	case GET_JFR_ID:
		ret = ubagg_get_jfr_id(dev, user_ctl);
		break;
	case GET_JETTY_ID:
		ret = ubagg_get_jetty_id(dev, user_ctl);
		break;
	case GET_SEG_INFO:
		ret = ubagg_get_seg_info(dev, user_ctl);
		break;
	case GET_JETTY_INFO:
		ret = ubagg_get_jetty_info(dev, user_ctl);
		break;
	default:
		ubagg_log_err("unsupported ubagg userctl opcde:%u",
			      user_ctl->in.opcode);
		ret = -ENXIO;
	}

	return ret;
}

int ubagg_config_device(struct ubcore_device *dev,
			struct ubcore_device_cfg *cfg)
{
	(void)dev;
	(void)cfg;
	return 0;
}

static struct ubcore_ucontext *
ubagg_alloc_ucontext(struct ubcore_device *dev, uint32_t eid_index,
		     struct ubcore_udrv_priv *udrv_data)
{
	(void)dev;
	(void)eid_index;
	(void)udrv_data;
	return kzalloc(sizeof(struct ubcore_ucontext), GFP_KERNEL);
}

static int ubagg_free_ucontext(struct ubcore_ucontext *uctx)
{
	kfree(uctx);
	return 0;
}

static int ubagg_query_device_attr(struct ubcore_device *dev,
				   struct ubcore_device_attr *attr)
{
	*attr = dev->attr;
	return 0;
}

struct ubcore_jfc *ubagg_create_jfc(struct ubcore_device *ub_dev,
				    struct ubcore_jfc_cfg *cfg,
				    struct ubcore_udata *udata)
{
	struct ubagg_device *ubagg_dev =
		ubagg_container_of(ub_dev, struct ubagg_device, ub_dev);
	struct ubagg_jfc *jfc;
	int id;

	if (ubagg_dev == NULL || ub_dev == NULL || cfg == NULL ||
	    udata == NULL || udata->uctx == NULL)
		return NULL;

	jfc = kzalloc(sizeof(struct ubagg_jfc), GFP_KERNEL);
	if (jfc == NULL)
		return NULL;
	jfc->base.jfc_cfg.depth = cfg->depth;
	spin_lock(&ubagg_dev->jfc_bitmap->lock);
	id = ubagg_bitmap_alloc_idx_from_offset_nolock(
		ubagg_dev->jfc_bitmap, ubagg_dev->jfc_bitmap->alloc_idx);
	if (id == -1) {
		spin_unlock(&ubagg_dev->jfc_bitmap->lock);
		ubagg_log_err("failed to alloc jfc_id");
		kfree(jfc);
		return NULL;
	}

	jfc->base.id = id;
	ubagg_dev->jfc_bitmap->alloc_idx =
		(jfc->base.id + 1) % UBAGG_BITMAP_MAX_SIZE;
	spin_unlock(&ubagg_dev->jfc_bitmap->lock);
	ubagg_log_info("ubagg jfc created successfully, id: %u.\n",
		       jfc->base.id);
	return &jfc->base;
}

int ubagg_destroy_jfc(struct ubcore_jfc *jfc)
{
	struct ubagg_device *ubagg_dev;
	struct ubagg_jfc *ubagg_jfc;
	int id;

	if (jfc == NULL || jfc->ub_dev == NULL)
		return -EINVAL;
	ubagg_dev = (struct ubagg_device *)jfc->ub_dev;
	ubagg_jfc = ubagg_container_of(jfc, struct ubagg_jfc, base);

	id = jfc->id;
	(void)ubagg_bitmap_free_idx(ubagg_dev->jfc_bitmap, id);
	kfree(ubagg_jfc);
	ubagg_log_info("ubagg jfc destroyed successfully, id: %u.\n", id);
	return 0;
}

struct ubcore_jfs *ubagg_create_jfs(struct ubcore_device *ub_dev,
				    struct ubcore_jfs_cfg *cfg,
				    struct ubcore_udata *udata)
{
	struct ubagg_device *ubagg_dev =
		ubagg_container_of(ub_dev, struct ubagg_device, ub_dev);
	struct ubagg_jfs *jfs;
	int id;

	if (ub_dev == NULL || cfg == NULL || udata == NULL ||
	    udata->uctx == NULL)
		return NULL;
	spin_lock(&ubagg_dev->jfs_bitmap->lock);
	id = ubagg_bitmap_alloc_idx_from_offset_nolock(
		ubagg_dev->jfs_bitmap, ubagg_dev->jfs_bitmap->alloc_idx);
	if (id == -1) {
		spin_unlock(&ubagg_dev->jfs_bitmap->lock);
		ubagg_log_err("failed to alloc jfs_id: id has been used up.\n");
		return NULL;
	}
	ubagg_dev->jfs_bitmap->alloc_idx = (id + 1) % UBAGG_BITMAP_MAX_SIZE;
	spin_unlock(&ubagg_dev->jfs_bitmap->lock);

	jfs = kzalloc(sizeof(struct ubagg_jfs), GFP_KERNEL);
	if (IS_ERR_OR_NULL(jfs)) {
		(void)ubagg_bitmap_free_idx(ubagg_dev->jfs_bitmap, id);
		return NULL;
	}

	jfs->base.jfs_cfg.depth = cfg->depth;
	jfs->base.jfs_cfg.max_sge = cfg->max_sge;
	jfs->base.jfs_cfg.max_rsge = cfg->max_rsge;
	jfs->base.jfs_cfg.max_inline_data = cfg->max_inline_data;
	jfs->base.jfs_cfg.trans_mode = cfg->trans_mode;
	jfs->base.jfs_id.id = id;

	ubagg_log_info("ubagg create jfs successfully, id: %u.\n",
		       jfs->base.jfs_id.id);
	return &jfs->base;
}

int ubagg_destroy_jfs(struct ubcore_jfs *jfs)
{
	struct ubagg_device *ubagg_dev;
	struct ubagg_jfs *ubagg_jfs;
	int id;

	if (jfs == NULL || jfs->ub_dev == NULL || jfs->uctx == NULL)
		return -EINVAL;
	ubagg_dev = (struct ubagg_device *)jfs->ub_dev;
	ubagg_jfs = ubagg_container_of(jfs, struct ubagg_jfs, base);
	id = jfs->jfs_id.id;
	(void)ubagg_bitmap_free_idx(ubagg_dev->jfs_bitmap, id);
	kfree(ubagg_jfs);
	ubagg_log_info("ubagg destroy jfs_ctx successfully, id: %u.\n", id);
	return 0;
}

struct ubcore_jfr *ubagg_create_jfr(struct ubcore_device *ub_dev,
				    struct ubcore_jfr_cfg *cfg,
				    struct ubcore_udata *udata)
{
	struct ubagg_device *ubagg_dev =
		ubagg_container_of(ub_dev, struct ubagg_device, ub_dev);
	struct ubagg_hash_table *ubagg_jfr_ht = NULL;
	struct ubagg_jfr_hash_node *tmp_jfr = NULL;
	struct ubagg_jfr_hash_node *jfr = NULL;
	int ret = 0;
	int id;

	if (ub_dev == NULL || cfg == NULL || udata == NULL ||
	    udata->uctx == NULL || cfg->id >= UBAGG_BITMAP_MAX_SIZE)
		return NULL;

	id = cfg->id;
	if (id == 0) {
		spin_lock(&ubagg_dev->jfr_bitmap->lock);
		id = ubagg_bitmap_alloc_idx_from_offset_nolock(
			ubagg_dev->jfr_bitmap,
			ubagg_dev->jfr_bitmap->alloc_idx);
		if (id == -1) {
			spin_unlock(&ubagg_dev->jfr_bitmap->lock);
			ubagg_log_err(
				"failed to alloc jfr_id: id has been used up.\n");
			return NULL;
		}
		ubagg_dev->jfr_bitmap->alloc_idx =
			(id + 1) % UBAGG_BITMAP_MAX_SIZE == 0 ?
				      BITMAP_OFFSET :
				      (id + 1) % UBAGG_BITMAP_MAX_SIZE;
		spin_unlock(&ubagg_dev->jfr_bitmap->lock);
	} else {
		if (ubagg_bitmap_use_id(ubagg_dev->jfr_bitmap, id) != 0) {
			ubagg_log_err(
				"failed to alloc jfr_id: id has been set.\n");
			return NULL;
		}
	}

	if (id == -1) {
		ubagg_log_err("failed to alloc jfr_id: id has been used up.\n");
		return NULL;
	}

	jfr = kzalloc(sizeof(struct ubagg_jfr_hash_node), GFP_KERNEL);
	if (jfr == NULL)
		goto FREE_ID;

	jfr->base.jfr_cfg.depth = cfg->depth;
	jfr->base.jfr_cfg.max_sge = cfg->max_sge;
	jfr->base.jfr_id.id = id;
	jfr->token_id = id;

	ret = copy_from_user(&jfr->ex_info,
			     (void __user *)udata->udrv_data->in_addr,
			     udata->udrv_data->in_len);
	if (ret != 0) {
		ubagg_log_err("ubagg fail to copy from user, ret:%d.\n", ret);
		goto FREE_JFR;
	}
	jfr->ex_info.base.id = id;

	ubagg_jfr_ht = &ubagg_dev->ubagg_ht[UBAGG_HT_JFR_HT];
	spin_lock(&ubagg_jfr_ht->lock);
	tmp_jfr = ubagg_hash_table_lookup_nolock(ubagg_jfr_ht, id, &id);
	if (tmp_jfr != NULL) {
		ubagg_log_err("id:%u already exists.\n", id);
		// should remove it
		ubagg_hash_table_remove_nolock(ubagg_jfr_ht, &tmp_jfr->hnode);
		spin_unlock(&ubagg_jfr_ht->lock);
		kfree(tmp_jfr);
		goto FREE_JFR;
	}

	ubagg_hash_table_add_nolock(ubagg_jfr_ht, &jfr->hnode, id);
	spin_unlock(&ubagg_jfr_ht->lock);

	ubagg_log_info("ubagg create jfr_ctx successfully, id: %u.\n",
		       jfr->base.jfr_id.id);
	return &jfr->base;

FREE_JFR:
	kfree(jfr);
FREE_ID:
	(void)ubagg_bitmap_free_idx(ubagg_dev->jfr_bitmap, id);

	ubagg_log_err("ubagg fail to create jfr.\n");
	return NULL;
}

int ubagg_destroy_jfr(struct ubcore_jfr *jfr)
{
	struct ubagg_device *ubagg_dev;
	struct ubagg_jfr_hash_node *ubagg_jfr;
	int id;

	if (jfr == NULL || jfr->ub_dev == NULL || jfr->uctx == NULL)
		return -EINVAL;
	ubagg_dev = (struct ubagg_device *)jfr->ub_dev;
	ubagg_jfr = ubagg_container_of(jfr, struct ubagg_jfr_hash_node, base);
	id = jfr->jfr_id.id;
	ubagg_hash_table_remove(&ubagg_dev->ubagg_ht[UBAGG_HT_JFR_HT],
				&ubagg_jfr->hnode);
	(void)ubagg_bitmap_free_idx(ubagg_dev->jfr_bitmap, id);
	kfree(ubagg_jfr);
	ubagg_log_info("ubagg destroy jfr_ctx successfully, id: %u.\n", id);
	return 0;
}

struct ubcore_jetty *ubagg_create_jetty(struct ubcore_device *dev,
					struct ubcore_jetty_cfg *cfg,
					struct ubcore_udata *udata)
{
	struct ubagg_device *ubagg_dev =
		ubagg_container_of(dev, struct ubagg_device, ub_dev);
	struct ubagg_hash_table *ubagg_jetty_ht = NULL;
	struct ubagg_jetty_hash_node *tmp_jetty = NULL;
	struct ubagg_jetty_hash_node *jetty = NULL;
	int ret;
	int id;

	if (dev == NULL || cfg == NULL || udata == NULL ||
	    cfg->id >= UBAGG_BITMAP_MAX_SIZE)
		return NULL;

	id = cfg->id;
	if (id == 0) {
		spin_lock(&ubagg_dev->jetty_bitmap->lock);
		id = ubagg_bitmap_alloc_idx_from_offset_nolock(
			ubagg_dev->jetty_bitmap,
			ubagg_dev->jetty_bitmap->alloc_idx);
		ubagg_log_err("jetty alloc bitmap, idx = %d\n", id);
		if (id <= 0) {
			spin_unlock(&ubagg_dev->jetty_bitmap->lock);
			ubagg_log_err("failed to alloc jetty_id.\n");
			return NULL;
		}
		ubagg_dev->jetty_bitmap->alloc_idx =
			(id + 1) % UBAGG_BITMAP_MAX_SIZE == 0 ?
				      BITMAP_OFFSET :
				      (id + 1) % UBAGG_BITMAP_MAX_SIZE;
		spin_unlock(&ubagg_dev->jetty_bitmap->lock);
	} else {
		if (ubagg_bitmap_use_id(ubagg_dev->jetty_bitmap, id) != 0) {
			ubagg_log_err(
				"failed to alloc jetty_id: id has been set.\n");
			return NULL;
		}
	}

	if (id == -1) {
		ubagg_log_err(
			"failed to alloc jetty_id: id has been used up.\n");
		return NULL;
	}

	jetty = kzalloc(sizeof(struct ubagg_jetty_hash_node), GFP_KERNEL);
	if (jetty == NULL)
		goto FREE_ID;

	jetty->base.jetty_cfg = *cfg;
	jetty->base.jetty_id.id = id;
	jetty->token_id = id;
	ret = copy_from_user(&jetty->ex_info,
			     (void __user *)udata->udrv_data->in_addr,
			     udata->udrv_data->in_len);
	if (ret != 0) {
		ubagg_log_err("ubagg fail to copy from user, ret:%d.\n", ret);
		goto FREE_JETTY;
	}
	jetty->ex_info.base.id = id;

	ubagg_jetty_ht = &ubagg_dev->ubagg_ht[UBAGG_HT_JETTY_HT];
	spin_lock(&ubagg_jetty_ht->lock);
	tmp_jetty = ubagg_hash_table_lookup_nolock(ubagg_jetty_ht, id, &id);
	if (tmp_jetty != NULL) {
		ubagg_log_err("id:%u already exists.\n", id);
		// should remove it
		ubagg_hash_table_remove_nolock(ubagg_jetty_ht,
					       &tmp_jetty->hnode);
		spin_unlock(&ubagg_jetty_ht->lock);
		kfree(tmp_jetty);
		goto FREE_ID;
	}

	ubagg_hash_table_add_nolock(ubagg_jetty_ht, &jetty->hnode, id);
	spin_unlock(&ubagg_jetty_ht->lock);

	ubagg_log_info("ubagg create jetty_ctx successfully, jetty_id: %d\n",
		       jetty->base.jetty_id.id);
	return &jetty->base;

FREE_JETTY:
	kfree(jetty);
FREE_ID:
	(void)ubagg_bitmap_free_idx(ubagg_dev->jetty_bitmap, id);

	ubagg_log_err("ubagg fail to create jetty_ctx.\n");
	return NULL;
}

int ubagg_destroy_jetty(struct ubcore_jetty *jetty)
{
	struct ubagg_jetty_hash_node *ubagg_jetty;
	struct ubagg_device *ubagg_dev;
	int id;

	if (jetty == NULL)
		return -EINVAL;
	ubagg_dev = (struct ubagg_device *)jetty->ub_dev;
	ubagg_jetty =
		ubagg_container_of(jetty, struct ubagg_jetty_hash_node, base);
	id = jetty->jetty_id.id;
	ubagg_hash_table_remove(&ubagg_dev->ubagg_ht[UBAGG_HT_JETTY_HT],
				&ubagg_jetty->hnode);
	(void)ubagg_bitmap_free_idx(ubagg_dev->jetty_bitmap, id);
	kfree(ubagg_jetty);
	ubagg_log_info("ubagg destroy jetty successfully, id: %u.\n", id);
	return 0;
}

int ubagg_query_device_status(struct ubcore_device *dev,
			      struct ubcore_device_status *status)
{
	int i;

	for (i = 0; i < UBCORE_MAX_PORT_CNT; ++i) {
		status->port_status[i].state = UBCORE_PORT_ACTIVE;
		status->port_status[i].active_mtu = UBCORE_MTU_4096;
		status->port_status[i].active_speed = UBCORE_SP_400G;
		status->port_status[i].active_width = UBCORE_LINK_X16;
	}
	return 0;
}

static struct ubcore_ops g_ubagg_dev_ops = {
	.owner = THIS_MODULE,
	.driver_name = "ub_agg",
	.abi_version = 0,
	.user_ctl = ubagg_user_ctl,
	.config_device = ubagg_config_device,
	.alloc_ucontext = ubagg_alloc_ucontext,
	.free_ucontext = ubagg_free_ucontext,
	.query_device_attr = ubagg_query_device_attr,
	.register_seg = ubagg_register_seg,
	.unregister_seg = ubagg_unregister_seg,
	.import_seg = ubagg_import_seg,
	.unimport_seg = ubagg_unimport_seg,
	.create_jfs = ubagg_create_jfs,
	.destroy_jfs = ubagg_destroy_jfs,
	.create_jfr = ubagg_create_jfr,
	.destroy_jfr = ubagg_destroy_jfr,
	.create_jetty = ubagg_create_jetty,
	.destroy_jetty = ubagg_destroy_jetty,
	.create_jfc = ubagg_create_jfc,
	.destroy_jfc = ubagg_destroy_jfc,
	.import_jfr = ubagg_import_jfr,
	.unimport_jfr = ubagg_unimport_jfr,
	.import_jetty = ubagg_import_jetty,
	.unimport_jetty = ubagg_unimport_jetty,
	.query_device_status = ubagg_query_device_status,
};

static void set_ubagg_device_attr(struct ubcore_device *dev,
				  struct ubagg_device_cap *dev_cap)
{
	dev->attr.dev_cap.feature = dev_cap->feature;
	dev->attr.dev_cap.max_jfc = dev_cap->max_jfc;
	dev->attr.dev_cap.max_jfs = dev_cap->max_jfs;
	dev->attr.dev_cap.max_jfr = dev_cap->max_jfr;
	dev->attr.dev_cap.max_jetty = dev_cap->max_jetty;
	dev->attr.dev_cap.max_jetty_grp = dev_cap->max_jetty_grp;
	dev->attr.dev_cap.max_jetty_in_jetty_grp =
		dev_cap->max_jetty_in_jetty_grp;
	dev->attr.dev_cap.max_jfc_depth = dev_cap->max_jfc_depth;
	dev->attr.dev_cap.max_jfs_depth = dev_cap->max_jfs_depth;
	dev->attr.dev_cap.max_jfr_depth = dev_cap->max_jfr_depth;
	dev->attr.dev_cap.max_jfs_inline_size = dev_cap->max_jfs_inline_size;
	dev->attr.dev_cap.max_jfs_sge = dev_cap->max_jfs_sge;
	dev->attr.dev_cap.max_jfs_rsge = dev_cap->max_jfs_rsge;
	dev->attr.dev_cap.max_jfr_sge = dev_cap->max_jfr_sge;
	dev->attr.dev_cap.max_msg_size = dev_cap->max_msg_size;
	dev->attr.dev_cap.max_read_size = dev_cap->max_read_size;
	dev->attr.dev_cap.max_write_size = dev_cap->max_write_size;
	dev->attr.dev_cap.max_cas_size = dev_cap->max_cas_size;
	dev->attr.dev_cap.max_swap_size = dev_cap->max_swap_size;
	dev->attr.dev_cap.max_fetch_and_add_size =
		dev_cap->max_fetch_and_add_size;
	dev->attr.dev_cap.max_fetch_and_sub_size =
		dev_cap->max_fetch_and_sub_size;
	dev->attr.dev_cap.max_fetch_and_and_size =
		dev_cap->max_fetch_and_and_size;
	dev->attr.dev_cap.max_fetch_and_or_size =
		dev_cap->max_fetch_and_or_size;
	dev->attr.dev_cap.max_fetch_and_xor_size =
		dev_cap->max_fetch_and_xor_size;
	dev->attr.dev_cap.atomic_feat = dev_cap->atomic_feat;
	dev->attr.dev_cap.trans_mode = dev_cap->trans_mode;
	dev->attr.dev_cap.sub_trans_mode_cap = dev_cap->sub_trans_mode_cap;
	dev->attr.dev_cap.congestion_ctrl_alg = dev_cap->congestion_ctrl_alg;
	dev->attr.dev_cap.ceq_cnt = dev_cap->congestion_ctrl_alg;
	dev->attr.dev_cap.max_tp_in_tpg = dev_cap->max_tp_in_tpg;
	dev->attr.dev_cap.max_eid_cnt = dev_cap->max_eid_cnt;
	dev->attr.dev_cap.page_size_cap = dev_cap->page_size_cap;
	dev->attr.dev_cap.max_oor_cnt = dev_cap->max_oor_cnt;
	dev->attr.dev_cap.mn = dev_cap->mn;
	dev->attr.dev_cap.max_netaddr_cnt = dev_cap->max_netaddr_cnt;
}

static void ubagg_reserve_jetty_id(struct ubagg_device *dev)
{
	if (ubagg_bitmap_alloc_idx(dev->jfs_bitmap) != 0)
		ubagg_log_err("Failed to reserve jfs id = 0.\n");

	if (ubagg_bitmap_alloc_idx(dev->jfr_bitmap) != 0)
		ubagg_log_err("Failed to reserve jfr id = 0.\n");

	if (ubagg_bitmap_alloc_idx(dev->jetty_bitmap) != 0)
		ubagg_log_err("Failed to reserve jetty id = 0.\n");
}

static int alloc_ubagg_dev_bitmap(struct ubagg_device *ubagg_dev)
{
	ubagg_dev->jfc_bitmap = ubagg_bitmap_alloc(UBAGG_BITMAP_MAX_SIZE);
	if (ubagg_dev->jfc_bitmap == NULL) {
		ubagg_log_err("failed alloc jfc bitmap.\n");
		return -1;
	}
	ubagg_dev->jfs_bitmap = ubagg_bitmap_alloc(UBAGG_BITMAP_MAX_SIZE);
	if (ubagg_dev->jfs_bitmap == NULL) {
		ubagg_log_err("failed alloc jfs bitmap.\n");
		goto free_jfc_bitmap;
	}
	ubagg_dev->jfr_bitmap = ubagg_bitmap_alloc(UBAGG_BITMAP_MAX_SIZE);
	if (ubagg_dev->jfr_bitmap == NULL) {
		ubagg_log_err("failed alloc jfr bitmap.\n");
		goto free_jfs_bitmap;
	}
	ubagg_dev->jfr_bitmap->alloc_idx = BITMAP_OFFSET;
	ubagg_dev->jetty_bitmap = ubagg_bitmap_alloc(UBAGG_BITMAP_MAX_SIZE);
	if (ubagg_dev->jetty_bitmap == NULL) {
		ubagg_log_err("failed alloc jetty bitmap.\n");
		goto free_jfr_bitmap;
	}
	ubagg_dev->jetty_bitmap->alloc_idx = BITMAP_OFFSET;
	ubagg_dev->segment_bitmap = ubagg_bitmap_alloc(UBAGG_BITMAP_MAX_SIZE);
	if (ubagg_dev->segment_bitmap == NULL) {
		ubagg_log_err("failed alloc seg bitmap.\n");
		goto free_jetty_bitmap;
	}
	ubagg_reserve_jetty_id(ubagg_dev);

	return 0;
free_jetty_bitmap:
	if (ubagg_dev->jetty_bitmap != NULL) {
		kfree(ubagg_dev->jetty_bitmap);
		ubagg_dev->jetty_bitmap = NULL;
	}
free_jfr_bitmap:
	if (ubagg_dev->jfr_bitmap != NULL) {
		kfree(ubagg_dev->jfr_bitmap);
		ubagg_dev->jfr_bitmap = NULL;
	}
free_jfs_bitmap:
	if (ubagg_dev->jfs_bitmap != NULL) {
		kfree(ubagg_dev->jfs_bitmap);
		ubagg_dev->jfs_bitmap = NULL;
	}
free_jfc_bitmap:
	if (ubagg_dev->jfc_bitmap != NULL) {
		kfree(ubagg_dev->jfc_bitmap);
		ubagg_dev->jfc_bitmap = NULL;
	}
	return -1;
}

static void free_ubagg_dev_bitmap(struct ubagg_device *ubagg_dev)
{
	ubagg_bitmap_free(ubagg_dev->segment_bitmap);
	ubagg_bitmap_free(ubagg_dev->jetty_bitmap);
	ubagg_bitmap_free(ubagg_dev->jfr_bitmap);
	ubagg_bitmap_free(ubagg_dev->jfs_bitmap);
	ubagg_bitmap_free(ubagg_dev->jfc_bitmap);
	ubagg_dev->segment_bitmap = NULL;
	ubagg_dev->jetty_bitmap = NULL;
	ubagg_dev->jfr_bitmap = NULL;
	ubagg_dev->jfs_bitmap = NULL;
	ubagg_dev->jfc_bitmap = NULL;
}

static struct ubagg_device *ubagg_dev_create(struct ubagg_add_dev *arg)
{
	struct ubagg_device *cur, *ubagg_dev = NULL;
	unsigned long flags;
	int ret, i;

	if (arg->in.slave_dev_num <= 0 ||
	    arg->in.slave_dev_num > UBAGG_MAX_DEV_NUM) {
		ubagg_log_err("slave dev num is invalid, slave_dev_num:%d\n",
			      arg->in.slave_dev_num);
		return NULL;
	}

	ubagg_dev = kzalloc(sizeof(struct ubagg_device), GFP_KERNEL);
	if (ubagg_dev == NULL)
		return NULL;
	kref_init(&ubagg_dev->ref);

	// init ubagg device
	(void)memcpy(ubagg_dev->master_dev_name, arg->in.master_dev_name,
		     UBAGG_MAX_DEV_NAME_LEN);
	ubagg_dev->slave_dev_num = arg->in.slave_dev_num;
	for (i = 0; i < arg->in.slave_dev_num; i++) {
		(void)memcpy(ubagg_dev->slave_dev_name[i],
			     arg->in.slave_dev_name[i], UBAGG_MAX_DEV_NAME_LEN);
	}

	// init ubcore_device
	(void)memcpy(ubagg_dev->ub_dev.dev_name, arg->in.master_dev_name,
		     UBAGG_MAX_DEV_NAME_LEN);
	ubagg_dev->ub_dev.ops = &g_ubagg_dev_ops;

	ubagg_dev->ub_dev.attr.tp_maintainer = false;
	ubagg_dev->ub_dev.attr.dev_cap.max_eid_cnt = UBAGG_DEVICE_MAX_EID_CNT;
	set_ubagg_device_attr(&ubagg_dev->ub_dev, &arg->in.dev_attr.dev_cap);

	ret = alloc_ubagg_dev_bitmap(ubagg_dev);
	if (ret != 0) {
		ubagg_log_err("ubagg alloc bitmap fail\n");
		ubagg_dev_ref_put(ubagg_dev);
		return NULL;
	}

	ret = ubcore_register_device(&ubagg_dev->ub_dev);
	if (ret != 0) {
		ubagg_log_err("ubcore register device fail, name:%s\n",
			      arg->in.master_dev_name);
		free_ubagg_dev_bitmap(ubagg_dev);
		ubagg_dev_ref_put(ubagg_dev);
		return NULL;
	}

	ubagg_dev->ub_dev.eid_table.eid_entries[0].eid_index = 0;
	ubagg_dev->ub_dev.eid_table.eid_entries[0].net = &init_net;
	(void)memcpy(&ubagg_dev->ub_dev.eid_table.eid_entries[0].eid,
		     &arg->in.eid, UBAGG_EID_SIZE);
	ubagg_dev->ub_dev.eid_table.eid_entries[0].valid = true;

	spin_lock_irqsave(&g_ubagg_dev_list_lock, flags);
	list_for_each_entry(cur, &g_ubagg_dev_list, list_node) {
		if (strncmp(cur->ub_dev.dev_name, arg->in.master_dev_name,
			    UBAGG_MAX_DEV_NAME_LEN) == 0) {
			spin_unlock_irqrestore(&g_ubagg_dev_list_lock, flags);
			ubagg_log_err("ubagg dev: %s exists in list\n",
				      arg->in.master_dev_name);
			ubcore_unregister_device(&ubagg_dev->ub_dev);
			free_ubagg_dev_bitmap(ubagg_dev);
			ubagg_dev_ref_put(ubagg_dev);
			return NULL;
		}
	}
	list_add_tail(&ubagg_dev->list_node, &g_ubagg_dev_list);
	spin_unlock_irqrestore(&g_ubagg_dev_list_lock, flags);
	ubagg_dev_ref_get(ubagg_dev);
	ubagg_log_info("ubagg dev: %s adds to list success\n",
		       arg->in.master_dev_name);
	return ubagg_dev;
}

static void ubagg_dev_destroy(char *name)
{
	struct ubagg_device *dev = NULL;
	unsigned long flags;
	bool dev_exist = false;

	spin_lock_irqsave(&g_ubagg_dev_list_lock, flags);
	list_for_each_entry(dev, &g_ubagg_dev_list, list_node) {
		if (strncmp(dev->ub_dev.dev_name, name,
			    UBAGG_MAX_DEV_NAME_LEN) == 0) {
			dev_exist = true;
			list_del(&dev->list_node);
			ubagg_dev_ref_put(dev);
			break;
		}
	}
	spin_unlock_irqrestore(&g_ubagg_dev_list_lock, flags);

	if (!dev_exist) {
		ubagg_log_err("ubagg device %s is not exist in list\n", name);
		return;
	}

	ubcore_unregister_device(&dev->ub_dev);
	free_ubagg_dev_bitmap(dev);
	ubagg_dev_ref_put(dev);
}

static int ubagg_check_add_dev_para_valid(struct ubagg_add_dev *arg)
{
	if (strnlen(arg->in.master_dev_name, UBAGG_MAX_DEV_NAME_LEN) >=
		UBAGG_MAX_DEV_NAME_LEN) {
		ubagg_log_err("invalid master dev name\n");
		return -EINVAL;
	}
	if (arg->in.slave_dev_num <= 0 || arg->in.slave_dev_num > UBAGG_MAX_DEV_NUM) {
		ubagg_log_err("slave dev num: %d is invalid\n", arg->in.slave_dev_num);
		return -EINVAL;
	}
	for (int i = 0; i < arg->in.slave_dev_num; i++) {
		if (strnlen(arg->in.slave_dev_name[i], UBAGG_MAX_DEV_NAME_LEN) >=
			UBAGG_MAX_DEV_NAME_LEN) {
			ubagg_log_err("invalid slave dev name, index: %d\n", i);
			return -EINVAL;
		}
	}

	return 0;
}

static int add_dev(struct ubagg_cmd_hdr *hdr)
{
	struct ubagg_device *ubagg_dev;
	struct ubagg_add_dev arg;
	int ret;

	if (hdr->args_len != sizeof(struct ubagg_add_dev)) {
		ubagg_log_err("add bond dev, hdr->args_len:%u is invalid\n",
			      hdr->args_len);
		return -EINVAL;
	}

	ret = copy_from_user(&arg, (void __user *)hdr->args_addr,
			     hdr->args_len);
	if (ret != 0) {
		ubagg_log_err("copy_from_user fail.");
		return ret;
	}

	if (ubagg_check_add_dev_para_valid(&arg) != 0) {
		ubagg_log_err("add bond dev, input para invalid\n");
		return -EINVAL;
	}

	if (ubagg_dev_exists(arg.in.master_dev_name)) {
		ubagg_log_err("ubagg dev already exist, name:%s\n",
			      arg.in.master_dev_name);
		return -EEXIST;
	}
	ubagg_dev = ubagg_dev_create(&arg);
	if (ubagg_dev == NULL) {
		ubagg_log_err("ubagg dev create fail, name:%s\n",
			      arg.in.master_dev_name);
		return -1;
	}

	if (!try_module_get(THIS_MODULE)) {
		ubagg_log_err("try_module_get for ubagg fail.\n");
		goto module_get_fail;
	}
	return 0;

module_get_fail:
	ubagg_dev_destroy(ubagg_dev->master_dev_name);
	return -ENODEV;
}

static int rmv_dev(struct ubagg_cmd_hdr *hdr)
{
	struct ubagg_rmv_dev arg;
	struct ubagg_device *ubagg_dev;
	int ret;

	if (hdr->args_len != sizeof(struct ubagg_rmv_dev)) {
		ubagg_log_err("rmv bond dev, hdr->args_len:%u is invalid\n",
			      hdr->args_len);
		return -EINVAL;
	}

	ret = copy_from_user(&arg, (void __user *)hdr->args_addr,
			     hdr->args_len);
	if (ret != 0) {
		ubagg_log_err("copy_from_user fail.");
		return ret;
	}

	if (strnlen(arg.in.master_dev_name, UBAGG_MAX_DEV_NAME_LEN) >=
		UBAGG_MAX_DEV_NAME_LEN) {
		ubagg_log_err("invalid master dev name\n");
		return -EINVAL;
	}

	ubagg_dev = ubagg_find_dev_by_name_and_rmv_from_list(
		arg.in.master_dev_name);
	if (ubagg_dev == NULL) {
		ubagg_log_err("ubagg dev not exist, name:%s\n",
			      arg.in.master_dev_name);
		return -ENODEV;
	}
	ubagg_log_info("rmv ubagg dev from list success\n");
	ubcore_unregister_device(&ubagg_dev->ub_dev);
	free_ubagg_dev_bitmap(ubagg_dev);
	ubagg_dev_ref_put(ubagg_dev);
	module_put(THIS_MODULE);
	return 0;
}

static bool is_eid_valid(const char *eid)
{
	int i;

	for (i = 0; i < EID_LEN; i++) {
		if (eid[i] != 0)
			return true;
	}
	return false;
}

static bool is_bonding_and_primary_eid_valid(struct ubagg_topo_map *topo_map)
{
	int i, j;
	bool has_primary_eid = false;

	for (i = 0; i < topo_map->node_num; i++) {
		if (!is_eid_valid(topo_map->topo_infos[i].bonding_eid))
			return false;
		has_primary_eid = false;
		for (j = 0; j < IODIE_NUM; j++) {
			if (is_eid_valid(topo_map->topo_infos[i]
						 .io_die_info[j]
						 .primary_eid))
				has_primary_eid = true;
		}
		if (!has_primary_eid)
			return false;
	}
	return true;
}

static int find_cur_node_index(struct ubagg_topo_map *topo_map,
			       uint32_t *node_index)
{
	int i;

	for (i = 0; i < topo_map->node_num; i++) {
		if (topo_map->topo_infos[i].is_cur_node) {
			*node_index = i;
			break;
		}
	}
	if (i == topo_map->node_num) {
		ubagg_log_err("can not find cur node index\n");
		return -1;
	}
	return 0;
}

static bool compare_eids(const char *eid1, const char *eid2)
{
	return memcmp(eid1, eid2, EID_LEN) == 0;
}

static int update_peer_port_eid(struct ubagg_topo_info *new_topo_info,
				struct ubagg_topo_info *old_topo_info)
{
	int i, j;
	char *new_peer_port_eid;
	char *old_peer_port_eid;

	for (i = 0; i < IODIE_NUM; i++) {
		for (j = 0; j < MAX_PORT_NUM; j++) {
			if (!is_eid_valid(
				    new_topo_info->io_die_info[i].port_eid[j]))
				continue;

			new_peer_port_eid =
				new_topo_info->io_die_info[i].peer_port_eid[j];
			old_peer_port_eid =
				old_topo_info->io_die_info[i].peer_port_eid[j];

			if (!is_eid_valid(new_peer_port_eid))
				continue;
			if (is_eid_valid(old_peer_port_eid) &&
			    !compare_eids(new_peer_port_eid,
					  old_peer_port_eid)) {
				ubagg_log_err(
					"peer port eid is not same, new: " EID_FMT
					", old: " EID_FMT "\n",
					EID_RAW_ARGS(new_peer_port_eid),
					EID_RAW_ARGS(old_peer_port_eid));
				return -1;
			}
			(void)memcpy(old_peer_port_eid, new_peer_port_eid,
				     EID_LEN);
		}
	}
	return 0;
}

static int ubagg_update_topo_info(struct ubagg_topo_map *new_topo_map,
				  struct ubagg_topo_map *old_topo_map)
{
	struct ubagg_topo_info *new_cur_node_info;
	struct ubagg_topo_info *old_cur_node_info;
	uint32_t new_cur_node_index = 0;
	uint32_t old_cur_node_index = 0;

	if (new_topo_map == NULL || old_topo_map == NULL) {
		ubagg_log_err("Invalid topo map\n");
		return -EINVAL;
	}
	if (!is_bonding_and_primary_eid_valid(new_topo_map)) {
		ubagg_log_err("Invalid primary eid\n");
		return -EINVAL;
	}
	if (find_cur_node_index(new_topo_map, &new_cur_node_index) != 0) {
		ubagg_log_err("find cur node index failed in new topo map\n");
		return -1;
	}
	new_cur_node_info = &(new_topo_map->topo_infos[new_cur_node_index]);
	if (find_cur_node_index(old_topo_map, &old_cur_node_index) != 0) {
		ubagg_log_err("find cur node index failed in old topo map\n");
		return -1;
	}
	old_cur_node_info = &(old_topo_map->topo_infos[old_cur_node_index]);

	if (update_peer_port_eid(new_cur_node_info, old_cur_node_info) != 0) {
		ubagg_log_err("update peer port eid failed\n");
		return -1;
	}
	return 0;
}

static bool has_add_dev_by_bonding_eid(const char *bonding_eid)
{
	int i;

	if (bonding_eid == NULL) {
		ubagg_log_err("bonding_eid is NULL");
		return false;
	}
	mutex_lock(&g_name_eid_arr_lock);
	for (i = 0; i < UBAGG_MAX_BONDING_DEV_NUM; i++) {
		if (compare_eids(bonding_eid, g_name_eid_arr[i].bonding_eid)) {
			mutex_unlock(&g_name_eid_arr_lock);
			return true;
		}
	}
	mutex_unlock(&g_name_eid_arr_lock);
	return false;
}

static void fill_add_dev_cfg(struct ubagg_topo_info *topo_info,
			     struct ubagg_add_dev_by_uvs *arg)
{
	int i, j, k;

	(void)memcpy(&arg->bonding_eid, topo_info->bonding_eid, EID_LEN);
	for (i = 0; i < IODIE_NUM; i++)
		(void)memcpy(&arg->slave_eid[i].primary_eid,
			     topo_info->io_die_info[i].primary_eid, EID_LEN);

	for (j = 0; j < IODIE_NUM; j++) {
		for (k = 0; k < MAX_PORT_NUM; k++)
			(void)memcpy(&arg->slave_eid[j].port_eid[k],
				     topo_info->io_die_info[j].port_eid[k],
				     EID_LEN);
	}
}

static void
set_ubagg_device_attr_by_ubcore_cap(struct ubcore_device *dev,
				    struct ubcore_device_cap *dev_cap)
{
	dev->attr.dev_cap = *dev_cap;
}

static int init_ubagg_dev(struct ubagg_device *ubagg_dev,
			  struct ubagg_add_dev_by_uvs *arg)
{
	struct ubcore_device *dev = NULL;
	int slave_dev_idx = 0;
	int i, j, k;

	// init ubagg device
	(void)memcpy(ubagg_dev->master_dev_name, arg->master_dev_name,
		     UBAGG_MAX_DEV_NAME_LEN);
	for (i = 0; i < IODIE_NUM; i++) {
		if (!is_eid_valid((char *)&arg->slave_eid[i].primary_eid.raw))
			continue;
		dev = ubcore_get_device_by_eid(&arg->slave_eid[i].primary_eid,
					       UBCORE_TRANSPORT_UB);
		if (dev == NULL) {
			ubagg_log_err(
				"primary slave %d dev not exist, eid: " EID_FMT
				"\n",
				i, EID_ARGS(arg->slave_eid[i].primary_eid));
			return -1;
		}
		if (slave_dev_idx == 0)
			set_ubagg_device_attr_by_ubcore_cap(&ubagg_dev->ub_dev,
							    &dev->attr.dev_cap);

		(void)memcpy(ubagg_dev->slave_dev_name[slave_dev_idx],
			     dev->dev_name, UBAGG_MAX_DEV_NAME_LEN);
		slave_dev_idx++;
	}

	for (j = 0; j < IODIE_NUM; j++) {
		for (k = 0; k < MAX_PORT_NUM; k++) {
			if (!is_eid_valid(
				    (char *)&arg->slave_eid[j].port_eid[k].raw))
				continue;
			dev = ubcore_get_device_by_eid(
				&arg->slave_eid[j].port_eid[k],
				UBCORE_TRANSPORT_UB);
			if (dev == NULL) {
				ubagg_log_err(
					"port slave %d_%d dev not exist, eid: " EID_FMT
					"\n",
					j, k,
					EID_ARGS(
						arg->slave_eid[j].port_eid[k]));
				return -1;
			}
			if (slave_dev_idx == 0)
				set_ubagg_device_attr_by_ubcore_cap(
					&ubagg_dev->ub_dev, &dev->attr.dev_cap);

			(void)memcpy(ubagg_dev->slave_dev_name[slave_dev_idx],
				     dev->dev_name, UBAGG_MAX_DEV_NAME_LEN);
			slave_dev_idx++;
		}
	}

	if (slave_dev_idx == 0) {
		ubagg_log_err("slave devs is null\n");
		return -1;
	}

	ubagg_dev->slave_dev_num = slave_dev_idx;
	return 0;
}

static int init_ubagg_res(struct ubagg_device *ubagg_dev)
{
	int ret = 0;
	int i = 0;
	int j = 0;

	ret = alloc_ubagg_dev_bitmap(ubagg_dev);
	if (ret != 0) {
		ubagg_log_err("ubagg alloc bitmap fail\n");
		return ret;
	}

	for (i = 0; i < UBAGG_HT_MAX; i++) {
		ret = ubagg_hash_table_alloc(&ubagg_dev->ubagg_ht[i],
					     &g_ubagg_ht_params[i]);
		if (ret != 0) {
			ubagg_log_err("Fail to init hash map:%d.\n", i);
			goto FREE_HMAP;
		}
	}

	return 0;

FREE_HMAP:
	for (j = 0; j < i; j++)
		ubagg_hash_table_free(&ubagg_dev->ubagg_ht[j]);
	free_ubagg_dev_bitmap(ubagg_dev);

	return -ENOMEM;
}

static void uninit_ubagg_res(struct ubagg_device *ubagg_dev)
{
	int i = 0;

	free_ubagg_dev_bitmap(ubagg_dev);
	for (i = 0; i < UBAGG_HT_MAX; i++)
		ubagg_hash_table_free(&ubagg_dev->ubagg_ht[i]);
}

static int init_ubagg_ubcore_dev(struct ubagg_device *ubagg_dev,
				 struct ubagg_add_dev_by_uvs *arg)
{
	int ret = 0;

	(void)memcpy(ubagg_dev->ub_dev.dev_name, arg->master_dev_name,
		     UBAGG_MAX_DEV_NAME_LEN);
	ubagg_dev->ub_dev.ops = &g_ubagg_dev_ops;
	ubagg_dev->ub_dev.attr.tp_maintainer = false;
	ubagg_dev->ub_dev.attr.dev_cap.max_eid_cnt = UBAGG_DEVICE_MAX_EID_CNT;

	ret = ubcore_register_device(&ubagg_dev->ub_dev);
	if (ret != 0) {
		ubagg_log_err("ubcore register device fail, name:%s\n",
			      arg->master_dev_name);
		free_ubagg_dev_bitmap(ubagg_dev);
		ubagg_dev_ref_put(ubagg_dev);
		return ret;
	}

	ubagg_dev->ub_dev.eid_table.eid_entries[0].eid_index = 0;
	ubagg_dev->ub_dev.eid_table.eid_entries[0].net = &init_net;
	(void)memcpy(&ubagg_dev->ub_dev.eid_table.eid_entries[0].eid,
		     &arg->bonding_eid, UBAGG_EID_SIZE);
	ubagg_dev->ub_dev.eid_table.eid_entries[0].valid = true;

	return 0;
}

static int add_dev_to_list(struct ubagg_device *ubagg_dev)
{
	struct ubagg_device *cur = NULL;
	unsigned long flags;

	spin_lock_irqsave(&g_ubagg_dev_list_lock, flags);
	list_for_each_entry(cur, &g_ubagg_dev_list, list_node) {
		if (strncmp(cur->ub_dev.dev_name, ubagg_dev->ub_dev.dev_name,
			    UBAGG_MAX_DEV_NAME_LEN) == 0) {
			spin_unlock_irqrestore(&g_ubagg_dev_list_lock, flags);
			ubcore_unregister_device(&ubagg_dev->ub_dev);
			free_ubagg_dev_bitmap(ubagg_dev);
			ubagg_dev_ref_put(ubagg_dev);
			return -EEXIST;
		}
	}
	list_add_tail(&ubagg_dev->list_node, &g_ubagg_dev_list);
	ubagg_dev_ref_get(ubagg_dev);
	spin_unlock_irqrestore(&g_ubagg_dev_list_lock, flags);
	return 0;
}

static void rmv_dev_from_list(struct ubagg_device *ubagg_dev)
{
	struct ubagg_device *cur = NULL;
	unsigned long flags;

	spin_lock_irqsave(&g_ubagg_dev_list_lock, flags);
	list_for_each_entry(cur, &g_ubagg_dev_list, list_node) {
		if (strncmp(cur->ub_dev.dev_name, ubagg_dev->ub_dev.dev_name,
			    UBAGG_MAX_DEV_NAME_LEN) == 0) {
			list_del(&cur->list_node);
			ubagg_dev_ref_put(ubagg_dev);
			spin_unlock_irqrestore(&g_ubagg_dev_list_lock, flags);
			return;
		}
	}
}

static int add_dev_by_uvs(struct ubagg_add_dev_by_uvs *arg)
{
	struct ubagg_device *ubagg_dev = NULL;

	if (ubagg_dev_exists(arg->master_dev_name)) {
		ubagg_log_err("ubagg dev already exist, name:%s\n",
			      arg->master_dev_name);
		return -EEXIST;
	}

	ubagg_dev = kzalloc(sizeof(struct ubagg_device), GFP_KERNEL);
	if (ubagg_dev == NULL)
		return -ENOMEM;
	kref_init(&ubagg_dev->ref);

	if (init_ubagg_dev(ubagg_dev, arg) != 0) {
		ubagg_log_err("init ubagg dev fail, name:%s\n",
			      arg->master_dev_name);
		goto PUT_DEV;
	}

	if (init_ubagg_res(ubagg_dev) != 0) {
		ubagg_log_err("init ubagg res fail, name:%s\n",
			      arg->master_dev_name);
		goto PUT_DEV;
	}

	if (init_ubagg_ubcore_dev(ubagg_dev, arg) != 0) {
		ubagg_log_err("init ubagg ubcore fail, name:%s\n",
			      arg->master_dev_name);
		goto UNINIT_UBAGG_RES;
	}

	if (add_dev_to_list(ubagg_dev) != 0) {
		ubagg_log_err("add dev to list fail, name:%s\n",
			      arg->master_dev_name);
		goto UNINIT_UBCORE_DEV;
	}

	if (!try_module_get(THIS_MODULE)) {
		ubagg_log_err("try_module_get for ubagg fail.\n");
		goto REMOVE_DEV_LIST;
	}
	return 0;

REMOVE_DEV_LIST:
	rmv_dev_from_list(ubagg_dev);
UNINIT_UBCORE_DEV:
	ubcore_unregister_device(&ubagg_dev->ub_dev);
UNINIT_UBAGG_RES:
	uninit_ubagg_res(ubagg_dev);
PUT_DEV:
	ubagg_dev_ref_put(ubagg_dev);

	return -ENODEV;
}

static bool is_eid_empty(const char *eid)
{
	int i;

	for (i = 0; i < EID_LEN; i++) {
		if (eid[i] != 0)
			return false;
	}
	return true;
}

static void find_add_master_dev(const char *bondingEid, const char *name)
{
	int i;
	int empty_index = -1;

	mutex_lock(&g_name_eid_arr_lock);
	for (i = 0; i < UBAGG_MAX_BONDING_DEV_NUM; i++) {
		if (is_eid_empty(g_name_eid_arr[i].bonding_eid)) {
			empty_index = i;
			break;
		}
	}
	if (empty_index == -1) {
		mutex_unlock(&g_name_eid_arr_lock);
		ubagg_log_err("g_name_eid_arr is full, max dev num is %d",
			      UBAGG_MAX_BONDING_DEV_NUM);
		return;
	}
	(void)memcpy(g_name_eid_arr[empty_index].bonding_eid, bondingEid,
		     EID_LEN);
	(void)snprintf(g_name_eid_arr[empty_index].master_dev_name,
		       UBAGG_MAX_DEV_NAME_LEN, "%s", name);
	mutex_unlock(&g_name_eid_arr_lock);
}

static int ubagg_add_dev_by_uvs(struct ubagg_topo_map *topo_map)
{
	struct ubagg_topo_info *cur_node_info;
	struct ubagg_add_dev_by_uvs arg = { 0 };
	char *master_dev_name = NULL;
	uint32_t cur_node_index = 0;

	if (find_cur_node_index(topo_map, &cur_node_index) != 0) {
		ubagg_log_err("find cur node index failed\n");
		return -1;
	}
	cur_node_info = &(topo_map->topo_infos[cur_node_index]);

	if (has_add_dev_by_bonding_eid(cur_node_info->bonding_eid)) {
		ubagg_log_info("has add dev by bonding eid: " EID_FMT "\n",
			       EID_RAW_ARGS(cur_node_info->bonding_eid));
		return 0;
	}

	master_dev_name = generate_master_dev_name();
	if (master_dev_name == NULL) {
		ubagg_log_err("generate master dev name failed\n");
		return -1;
	}

	(void)snprintf(arg.master_dev_name, UBAGG_MAX_DEV_NAME_LEN, "%s",
		       master_dev_name);
	fill_add_dev_cfg(cur_node_info, &arg);

	if (add_dev_by_uvs(&arg) != 0) {
		release_bond_device_id_with_name(master_dev_name);
		kfree(master_dev_name);
		ubagg_log_err("add ubagg dev by uvs failed\n");
		return -1;
	}
	find_add_master_dev(cur_node_info->bonding_eid, master_dev_name);
	kfree(master_dev_name);
	return 0;
}

static void print_topo_map(struct ubagg_topo_map *topo_map)
{
	int i, j, k;
	struct ubagg_topo_info *cur_node_info;

	ubagg_log_info(
		"========================== topo map start =============================\n");
	for (i = 0; i < topo_map->node_num; i++) {
		cur_node_info = topo_map->topo_infos + i;
		if (is_eid_empty(cur_node_info->bonding_eid))
			continue;

		ubagg_log_info(
			"===================== node %d start =======================\n",
			i);
		ubagg_log_info("bonding eid: " EID_FMT "\n",
			       EID_RAW_ARGS(cur_node_info->bonding_eid));
		for (j = 0; j < IODIE_NUM; j++) {
			ubagg_log_info(
				"\tprimary eid %d: " EID_FMT "\n", j,
				EID_RAW_ARGS(cur_node_info->io_die_info[j]
						     .primary_eid));
			for (k = 0; k < MAX_PORT_NUM; k++) {
				ubagg_log_info(
					"\t\tport eid %d: " EID_FMT "\n", k,
					EID_RAW_ARGS(
						cur_node_info->io_die_info[j]
							.port_eid[k]));
				ubagg_log_info(
					"\t\tpeer_port eid %d: " EID_FMT "\n",
					k,
					EID_RAW_ARGS(
						cur_node_info->io_die_info[j]
							.peer_port_eid[k]));
			}
		}
		ubagg_log_info(
			"===================== node %d end =======================\n",
			i);
	}
	ubagg_log_info(
		"========================== topo map end =============================\n");
}

static int ubagg_set_topo_info(struct ubagg_cmd_hdr *hdr)
{
	struct ubagg_set_topo_info arg;
	struct ubagg_topo_map *new_topo_map;
	struct ubagg_topo_map *topo_map;
	int ret;

	if (hdr->args_len != sizeof(struct ubagg_set_topo_info)) {
		ubagg_log_err(
			"set topo info, args_len is invalid, args_len:%u\n",
			hdr->args_len);
		return -EINVAL;
	}

	ret = copy_from_user(&arg, (void __user *)hdr->args_addr,
			     hdr->args_len);
	if (ret != 0) {
		ubagg_log_err("copy_from_user fail.");
		return ret;
	}
	if (arg.in.topo == NULL || arg.in.topo_num == 0 ||
	    arg.in.topo_num > MAX_NODE_NUM) {
		ubagg_log_err("Invalid set_topo_info param\n");
		return -EINVAL;
	}
	topo_map = get_global_ubagg_map();
	if (topo_map == NULL) {
		topo_map = create_global_ubagg_topo_map(arg.in.topo,
							arg.in.topo_num);
		if (topo_map == NULL) {
			ubagg_log_err("Failed to create topo map\n");
			return -ENOMEM;
		}
		if (!is_bonding_and_primary_eid_valid(topo_map)) {
			delete_global_ubagg_topo_map();
			ubagg_log_err("Invalid primary eid\n");
			return -EINVAL;
		}
	} else {
		// update topo_map
		new_topo_map = create_ubagg_topo_map_from_user(arg.in.topo,
							       arg.in.topo_num);
		if (ubagg_update_topo_info(new_topo_map, topo_map) != 0) {
			delete_ubagg_topo_map(new_topo_map);
			ubagg_log_err("Failed to update topo info\n");
			return -1;
		}
		delete_ubagg_topo_map(new_topo_map);
	}

	print_topo_map(topo_map);

	if (ubagg_add_dev_by_uvs(topo_map) != 0) {
		delete_global_ubagg_topo_map();
		ubagg_log_err("Failed to add dev by uvs\n");
		return -1;
	}
	return 0;
}

void ubagg_delete_topo_map(void)
{
	delete_global_ubagg_topo_map();
}

long ubagg_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct ubagg_cmd_hdr hdr;
	int ret = 0;

	if (cmd != UBAGG_CMD || !capable(CAP_NET_ADMIN)) {
		ubagg_log_err("bad ubagg ioctl cmd!");
		return -ENOIOCTLCMD;
	}

	ret = copy_from_user(&hdr, (void *)arg, sizeof(struct ubagg_cmd_hdr));
	if (ret != 0) {
		ubagg_log_err("copy from user fail, ret:%d", ret);
		return -EFAULT;
	}
	switch (hdr.command) {
	case UBAGG_ADD_DEV:
		return add_dev(&hdr);
	case UBAGG_RMV_DEV:
		return rmv_dev(&hdr);
	case UBAGG_SET_TOPO_INFO:
		return ubagg_set_topo_info(&hdr);
	default:
		ubagg_log_err("Wrong command type:%u", hdr.command);
		return -EINVAL;
	}
}

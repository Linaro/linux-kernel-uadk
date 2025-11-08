// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2021-2025. All rights reserved.
 *
 * Description: ubcore device add and remove ops file
 * Author: Qian Guoxin
 * Create: 2021-08-03
 * Note:
 * History: 2021-08-03: create file
 */

#include <linux/list.h>
#include "ubcore_priv.h"

#define UBCORE_DEVICE_NAME "ubcore"

static LIST_HEAD(g_mue_cdev_list);
static DECLARE_RWSEM(g_mue_cdev_rwsem);

static LIST_HEAD(g_client_list);
static LIST_HEAD(g_device_list);

/*
 * g_device_rwsem and g_lists_rwsem protect both g_device_list and g_client_list.
 * g_device_rwsem protects writer access by device and client
 * g_lists_rwsem protects reader access to these lists.
 * Iterators of these lists must lock it for read, while updates
 * to the lists must be done with a write lock.
 */
static DECLARE_RWSEM(g_device_rwsem);

/*
 * g_clients_rwsem protect g_client_list.
 */
static DECLARE_RWSEM(g_clients_rwsem);

struct ubcore_device *ubcore_find_device(union ubcore_eid *eid,
					 enum ubcore_transport_type type)
{
	struct ubcore_device *dev, *target = NULL;
	uint32_t idx;

	down_read(&g_device_rwsem);
	list_for_each_entry(dev, &g_device_list, list_node) {
		if (IS_ERR_OR_NULL(dev->eid_table.eid_entries))
			continue;
		for (idx = 0; idx < dev->attr.dev_cap.max_eid_cnt; idx++) {
			if (memcmp(&dev->eid_table.eid_entries[idx].eid, eid,
				   sizeof(union ubcore_eid)) == 0 &&
			    dev->transport_type == type) {
				target = dev;
				ubcore_get_device(target);
				break;
			}
		}
		if (target != NULL)
			break;
	}
	up_read(&g_device_rwsem);
	return target;
}

void ubcore_put_devices(struct ubcore_device **devices, uint32_t cnt)
{
	uint32_t i;

	if (devices == NULL)
		return;

	for (i = 0; i < cnt; i++)
		ubcore_put_device(devices[i]);

	kfree(devices);
}

void ubcore_get_device(struct ubcore_device *dev)
{
	if (IS_ERR_OR_NULL(dev)) {
		ubcore_log_err("Invalid parameter\n");
		return;
	}

	atomic_inc(&dev->use_cnt);
}

void ubcore_put_device(struct ubcore_device *dev)
{
	if (IS_ERR_OR_NULL(dev)) {
		ubcore_log_err("Invalid parameter\n");
		return;
	}

	if (atomic_dec_and_test(&dev->use_cnt))
		complete(&dev->comp);
}

int ubcore_get_tp_list(struct ubcore_device *dev, struct ubcore_get_tp_cfg *cfg,
		       uint32_t *tp_cnt, struct ubcore_tp_info *tp_list,
		       struct ubcore_udata *udata)
{
	int ret;

	if (dev == NULL || dev->ops == NULL || dev->ops->get_tp_list == NULL ||
	    cfg == NULL || tp_cnt == NULL || tp_list == NULL || *tp_cnt == 0) {
		ubcore_log_err("Invalid parameter.\n");
		return -EINVAL;
	}

	if (ubcore_check_trans_mode_valid(cfg->trans_mode) != true) {
		ubcore_log_err("Invalid parameter, trans_mode: %d.\n",
			       (int)cfg->trans_mode);
		return -EINVAL;
	}

	ret = dev->ops->get_tp_list(dev, cfg, tp_cnt, tp_list, udata);
	if (ret != 0)
		ubcore_log_err("Failed to get to list, ret: %d.\n", ret);

	return ret;
}
EXPORT_SYMBOL(ubcore_get_tp_list);

struct ubcore_tjetty *
ubcore_import_jfr_ex(struct ubcore_device *dev, struct ubcore_tjetty_cfg *cfg,
		     struct ubcore_active_tp_cfg *active_tp_cfg,
		     struct ubcore_udata *udata)
{
	struct ubcore_tjetty *tjfr;

	if (dev == NULL || dev->ops == NULL ||
	    dev->ops->import_jfr_ex == NULL || dev->ops->unimport_jfr == NULL ||
	    cfg == NULL || active_tp_cfg == NULL ||
	    dev->attr.dev_cap.max_eid_cnt <= cfg->eid_index)
		return ERR_PTR(-EINVAL);

	tjfr = dev->ops->import_jfr_ex(dev, cfg, active_tp_cfg, udata);
	if (IS_ERR_OR_NULL(tjfr)) {
		ubcore_log_err("UBEP failed to import jfr, jfr_id:%u.\n",
			       cfg->id.id);
		if (tjfr == NULL)
			return ERR_PTR(-ENOEXEC);
		return tjfr;
	}
	tjfr->cfg = *cfg;
	tjfr->ub_dev = dev;
	tjfr->uctx = ubcore_get_uctx(udata);
	atomic_set(&tjfr->use_cnt, 0);
	mutex_init(&tjfr->lock);
	return tjfr;
}
EXPORT_SYMBOL(ubcore_import_jfr_ex);

struct ubcore_tjetty *
ubcore_import_jetty_ex(struct ubcore_device *dev, struct ubcore_tjetty_cfg *cfg,
		       struct ubcore_active_tp_cfg *active_tp_cfg,
		       struct ubcore_udata *udata)
{
	struct ubcore_tjetty *tjetty;

	if (dev == NULL || dev->ops == NULL ||
	    dev->ops->import_jetty_ex == NULL ||
	    dev->ops->unimport_jetty == NULL || cfg == NULL ||
	    active_tp_cfg == NULL ||
	    dev->attr.dev_cap.max_eid_cnt <= cfg->eid_index)
		return ERR_PTR(-EINVAL);

	tjetty = dev->ops->import_jetty_ex(dev, cfg, active_tp_cfg, udata);
	if (IS_ERR_OR_NULL(tjetty)) {
		ubcore_log_err("UBEP failed to import jetty, jetty_id:%u.\n",
			       cfg->id.id);
		return UBCORE_CHECK_RETURN_ERR_PTR(tjetty, ENOEXEC);
	}
	tjetty->cfg = *cfg;
	tjetty->ub_dev = dev;
	tjetty->uctx = ubcore_get_uctx(udata);

	atomic_set(&tjetty->use_cnt, 0);
	mutex_init(&tjetty->lock);
	return tjetty;
}
EXPORT_SYMBOL(ubcore_import_jetty_ex);

static int ubcore_inner_bind_ub_jetty_ctrlplane(
	struct ubcore_jetty *jetty, struct ubcore_tjetty *tjetty,
	struct ubcore_active_tp_cfg *active_tp_cfg, struct ubcore_udata *udata)
{
	struct ubcore_device *dev;
	int ret;

	dev = jetty->ub_dev;
	if (dev->ops == NULL || dev->ops->bind_jetty_ex == NULL ||
	    dev->ops->unbind_jetty == NULL) {
		ubcore_log_err(
			"Failed to bind jetty, no ops->bind_jetty_ex.\n");
		return -1;
	}

	ret = dev->ops->bind_jetty_ex(jetty, tjetty, active_tp_cfg, udata);
	if (ret != 0) {
		ubcore_log_err("Failed to bind jetty.\n");
		return ret;
	}
	atomic_inc(&jetty->use_cnt);
	return 0;
}

static int ubcore_inner_bind_jetty_ctrlplane(
	struct ubcore_jetty *jetty, struct ubcore_tjetty *tjetty,
	struct ubcore_active_tp_cfg *active_tp_cfg, struct ubcore_udata *udata)
{
	struct ubcore_device *dev;
	int ret;

	dev = jetty->ub_dev;
	if (dev->attr.dev_cap.max_eid_cnt <= tjetty->cfg.eid_index) {
		ubcore_log_err("eid_index:%u is beyond the max_eid_cnt:%u.\n",
			       tjetty->cfg.eid_index,
			       dev->attr.dev_cap.max_eid_cnt);
		return -EINVAL;
	}

	if (dev->transport_type != UBCORE_TRANSPORT_UB) {
		ubcore_log_err("Invalid transport_type: %d.\n",
			       dev->transport_type);
		return -EINVAL;
	}

	ret = ubcore_inner_bind_ub_jetty_ctrlplane(jetty, tjetty, active_tp_cfg,
						   udata);
	if (ret != 0)
		return ret;

	ubcore_log_info_rl("jetty: %u bind tjetty: %u\n", jetty->jetty_id.id,
			   tjetty->cfg.id.id);
	jetty->remote_jetty = tjetty;
	atomic_inc(&tjetty->use_cnt);
	return 0;
}

int ubcore_bind_jetty_ex(struct ubcore_jetty *jetty,
			 struct ubcore_tjetty *tjetty,
			 struct ubcore_active_tp_cfg *active_tp_cfg,
			 struct ubcore_udata *udata)
{
	if (jetty == NULL || tjetty == NULL || jetty->ub_dev == NULL ||
	    jetty->ub_dev->ops == NULL || active_tp_cfg == NULL) {
		ubcore_log_err("Invalid parameter.\n");
		return -1;
	}
	if ((jetty->jetty_cfg.trans_mode != UBCORE_TP_RC) ||
	    (tjetty->cfg.trans_mode != UBCORE_TP_RC)) {
		ubcore_log_err("trans mode is not rc type.\n");
		return -1;
	}
	if (jetty->remote_jetty == tjetty) {
		ubcore_log_info("bind reentry, jetty: %u bind tjetty: %u.\n",
				jetty->jetty_id.id, tjetty->cfg.id.id);
		return 0;
	}
	if (jetty->remote_jetty != NULL) {
		ubcore_log_err(
			"The same jetty, different tjetty, prevent duplicate bind.\n");
		return -1;
	}

	if (tjetty->vtpn != NULL &&
	    (!is_create_rc_shared_tp(tjetty->cfg.trans_mode,
				     tjetty->cfg.flag.bs.order_type,
				     tjetty->cfg.flag.bs.share_tp))) {
		ubcore_log_err(
			"The tjetty, has already connect vtpn, prevent duplicate bind.\n");
		return -1;
	}

	return ubcore_inner_bind_jetty_ctrlplane(jetty, tjetty, active_tp_cfg,
						 udata);
}
EXPORT_SYMBOL(ubcore_bind_jetty_ex);

int ubcore_user_control(struct ubcore_device *dev,
			struct ubcore_user_ctl *k_user_ctl)
{
	int ret;

	if (k_user_ctl == NULL) {
		ubcore_log_err("invalid parameter with input nullptr.\n");
		return -1;
	}

	if (dev == NULL || dev->ops == NULL || dev->ops->user_ctl == NULL) {
		ubcore_log_err("invalid parameter with dev nullptr.\n");
		return -1;
	}

	ret = dev->ops->user_ctl(dev, k_user_ctl);
	if (ret != 0) {
		ubcore_log_err("failed to exec kdrv_user_ctl in %s.\n",
			       __func__);
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL(ubcore_user_control);

int ubcore_config_rsvd_jetty(struct ubcore_device *dev, uint32_t min_jetty_id,
			     uint32_t max_jetty_id)
{
	struct ubcore_device_cfg cfg = { 0 };
	int ret = 0;

	if (dev == NULL || dev->ops == NULL ||
	    dev->ops->config_device == NULL ||
	    dev->ops->query_device_attr == NULL ||
	    dev->transport_type != UBCORE_TRANSPORT_UB) {
		return -EINVAL;
	}

	cfg.ue_idx = dev->attr.ue_idx;
	cfg.mask.bs.reserved_jetty_id_min = 1;
	cfg.mask.bs.reserved_jetty_id_max = 1;
	cfg.reserved_jetty_id_min = min_jetty_id;
	cfg.reserved_jetty_id_max = max_jetty_id;

	ret = dev->ops->config_device(dev, &cfg);
	if (ret) {
		ubcore_log_info("dev:%s, not support reserved jetty\n",
				dev->dev_name);
		dev->attr.reserved_jetty_id_max = U32_MAX;
		dev->attr.reserved_jetty_id_min = U32_MAX;
	} else {
		dev->ops->query_device_attr(dev, &dev->attr);
	}

	return ret;
}

int ubcore_query_device_attr(struct ubcore_device *dev,
			     struct ubcore_device_attr *attr)
{
	int ret;

	if (dev == NULL || attr == NULL || dev->ops == NULL ||
	    dev->ops->query_device_attr == NULL) {
		ubcore_log_err("Invalid argument.\n");
		return -EINVAL;
	}

	ret = dev->ops->query_device_attr(dev, attr);
	if (ret != 0) {
		ubcore_log_err("failed to query device attr, ret: %d.\n", ret);
		return -EPERM;
	}
	return 0;
}
EXPORT_SYMBOL(ubcore_query_device_attr);

int ubcore_query_device_status(struct ubcore_device *dev,
			       struct ubcore_device_status *status)
{
	int ret;

	if (dev == NULL || status == NULL || dev->ops == NULL ||
	    dev->ops->query_device_status == NULL) {
		ubcore_log_err("Invalid argument.\n");
		return -EINVAL;
	}

	ret = dev->ops->query_device_status(dev, status);
	if (ret != 0) {
		ubcore_log_err("failed to query device status, ret: %d.\n",
			       ret);
		return -EPERM;
	}
	return 0;
}
EXPORT_SYMBOL(ubcore_query_device_status);


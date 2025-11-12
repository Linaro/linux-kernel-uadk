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

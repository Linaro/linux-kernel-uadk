// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 *
 * Description: ubagg topo info file
 * Author: Ma Chuan
 * Create: 2025-06-07
 * Note:
 * History: 2025-06-07 Create file
 */
#include <linux/slab.h>
#include <linux/uaccess.h>
#include "ubagg_log.h"
#include "ubagg_topo_info.h"

static struct ubagg_topo_map *g_topo_map;

struct ubagg_topo_map *
create_global_ubagg_topo_map(struct ubagg_topo_info *topo_infos,
			     uint32_t node_num)
{
	g_topo_map = create_ubagg_topo_map_from_user(topo_infos, node_num);
	return g_topo_map;
}

void delete_global_ubagg_topo_map(void)
{
	if (g_topo_map == NULL)
		return;
	delete_ubagg_topo_map(g_topo_map);
	g_topo_map = NULL;
}

struct ubagg_topo_map *get_global_ubagg_map(void)
{
	return g_topo_map;
}

struct ubagg_topo_map *
create_ubagg_topo_map_from_user(struct ubagg_topo_info *user_topo_infos,
				uint32_t node_num)
{
	struct ubagg_topo_map *topo_map = NULL;
	int ret = 0;

	if (user_topo_infos == NULL || node_num <= 0 ||
	    node_num > MAX_NODE_NUM) {
		ubagg_log_err("Invalid param\n");
		return NULL;
	}
	topo_map = kzalloc(sizeof(struct ubagg_topo_map), GFP_KERNEL);
	if (topo_map == NULL)
		return NULL;
	ret = copy_from_user(topo_map->topo_infos,
			     (void __user *)user_topo_infos,
			     sizeof(struct ubagg_topo_info) * node_num);
	if (ret != 0) {
		ubagg_log_err("Failed to copy topo info\n");
		kfree(topo_map);
		return NULL;
	}
	topo_map->node_num = node_num;
	return topo_map;
}

void delete_ubagg_topo_map(struct ubagg_topo_map *topo_map)
{
	if (topo_map == NULL)
		return;
	kfree(topo_map);
}

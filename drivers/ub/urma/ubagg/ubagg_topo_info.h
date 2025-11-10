/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 *
 * Description: ubagg topo info head file
 * Author: Ma Chuan
 * Create: 2025-06-07
 * Note:
 * History: 2025-06-07 Create file
 */
#ifndef UBAGG_TOPO_INFO_H
#define UBAGG_TOPO_INFO_H

#include <linux/types.h>

#define EID_LEN (16)
#define MAX_PORT_NUM (9)
#define MAX_NODE_NUM (16)
#define IODIE_NUM (2)

struct ubagg_iodie_info {
	char primary_eid[EID_LEN];
	char port_eid[MAX_PORT_NUM][EID_LEN];
	char peer_port_eid[MAX_PORT_NUM][EID_LEN];
	int socket_id;
};

struct ubagg_topo_info {
	char bonding_eid[EID_LEN];
	struct ubagg_iodie_info io_die_info[IODIE_NUM];
	bool is_cur_node;
};

struct ubagg_topo_map {
	struct ubagg_topo_info topo_infos[MAX_NODE_NUM];
	uint32_t node_num;
};

struct ubagg_topo_map *
create_global_ubagg_topo_map(struct ubagg_topo_info *topo_infos,
			     uint32_t node_num);

void delete_global_ubagg_topo_map(void);

struct ubagg_topo_map *get_global_ubagg_map(void);

struct ubagg_topo_map *
create_ubagg_topo_map_from_user(struct ubagg_topo_info *topo_infos,
				uint32_t node_num);

void delete_ubagg_topo_map(struct ubagg_topo_map *topo_map);
#endif // UBAGG_TOPO_INFO_H

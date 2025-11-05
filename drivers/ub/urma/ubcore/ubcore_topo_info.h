/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 *
 * Description: ubcore topo info head file
 * Author: Liu Jiajun
 * Create: 2025-07-03
 * Note:
 * History: 2025-07-03 Create file
 */

#ifndef UBCORE_TOPO_INFO_H
#define UBCORE_TOPO_INFO_H

#include <linux/types.h>

#define EID_LEN (16)
#define MAX_PORT_NUM (9)
#define MAX_NODE_NUM (16)
#define IODIE_NUM (2)

struct ubcore_iodie_info {
	char primary_eid[EID_LEN];
	char port_eid[MAX_PORT_NUM][EID_LEN];
	char peer_port_eid[MAX_PORT_NUM][EID_LEN];
	int socket_id;
};

struct ubcore_topo_info {
	char bonding_eid[EID_LEN];
	struct ubcore_iodie_info io_die_info[IODIE_NUM];
	bool is_cur_node;
};

struct ubcore_topo_map {
	struct ubcore_topo_info topo_infos[MAX_NODE_NUM];
	uint32_t node_num;
};

struct ubcore_topo_map *
ubcore_create_global_topo_map(struct ubcore_topo_info *topo_infos,
			      uint32_t node_num);
void ubcore_delete_global_topo_map(void);
struct ubcore_topo_map *ubcore_get_global_topo_map(void);
struct ubcore_topo_map *
ubcore_create_topo_map_from_user(struct ubcore_topo_info *user_topo_infos,
				 uint32_t node_num);
void ubcore_delete_topo_map(struct ubcore_topo_map *topo_map);
bool is_eid_valid(const char *eid);
bool is_bonding_and_primary_eid_valid(struct ubcore_topo_map *topo_map);
struct ubcore_topo_info *
ubcore_get_cur_topo_info(struct ubcore_topo_map *topo_map);
int ubcore_update_topo_map(struct ubcore_topo_map *new_topo_map,
			   struct ubcore_topo_map *old_topo_map);
void ubcore_show_topo_map(struct ubcore_topo_map *topo_map);
int ubcore_get_primary_eid(union ubcore_eid *eid,
			   union ubcore_eid *primary_eid);

int ubcore_get_primary_eid_by_bonding_eid(union ubcore_eid *bonding_eid,
	union ubcore_eid *primary_eid);

#endif // UBCORE_TOPO_INFO_H

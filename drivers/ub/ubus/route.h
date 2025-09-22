/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) HiSilicon Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef __ROUTE_H__
#define __ROUTE_H__

struct ub_entity;
struct ub_port;
void ub_route_clear(struct ub_entity *uent);
int ub_route_add_entry(struct ub_port *port, u32 cna, short distance);
bool ub_entity_support_forward(struct ub_entity *uent);
int ub_route_mod_neighbor(struct ub_port *port, struct ub_port *r_port);
void ub_route_clear_port(struct ub_port *port);
void ub_route_mod_bfs(struct ub_entity *uent);
void ub_route_del_bfs(struct ub_entity *uent);
void ub_route_sync_dev(struct ub_entity *uent);
void ub_route_sync_all(void);
int ub_route_entities(struct list_head *dev_list);
void ub_route_table_clear_for_port(struct ub_port *port,
				   struct ub_port *r_port);
int ub_route_table_set_for_port(struct ub_port *port, struct ub_port *r_port);

#endif /* __ROUTE_H__ */

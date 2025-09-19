/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) HiSilicon Technologies Co., Ltd. 2025. All rights reserved.
 */
#ifndef __PORT_H__
#define __PORT_H__

#define for_each_uent_port(p, d) \
	for ((p) = (d)->ports; ((p) - (d)->ports) < (d)->port_nums; (p)++)

enum ub_share_port_notify_type {
	RESET_PREPARE,
	RESET_DONE,
	NOTIFY_TYPE_MAX
};

struct ub_port;
struct ub_entity;
void ub_port_disconnect(struct ub_port *port);
void ub_port_connect(struct ub_port *port, struct ub_port *r_port);
bool ub_check_and_connect(struct ub_port *port, struct ub_entity *r_uent);
int ub_ports_add(struct ub_entity *uent);
void ub_ports_del(struct ub_entity *uent);
int ub_ports_setup(struct ub_entity *uent);
void ub_ports_unset(struct ub_entity *uent);

#endif /* __PORT_H__ */

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
void ub_route_sync_dev(struct ub_entity *uent);

#endif /* __ROUTE_H__ */

/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) HiSilicon Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef __RESET_H__
#define __RESET_H__

struct ub_port;
struct ub_entity;

int ub_port_reset_function(struct ub_port *port);
int ub_port_reset(struct ub_entity *dev, int port_id);
int ub_port_reset_check(struct ub_entity *dev, int port_id);

#endif /* __RESET_H__ */

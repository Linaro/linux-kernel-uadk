/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) HiSilicon Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef __UBUS_CONTROLLER_H__
#define __UBUS_CONTROLLER_H__

struct ub_bus_controller;
struct ub_bus_controller_ops {
	int (*entity_enable)(struct ub_entity *uent, u8 enable);
};

struct ub_bus_controller *ub_find_bus_controller_by_cna(u32 cna);
void ub_bus_controllers_remove(void);
int ub_bus_controllers_probe(void);
int ub_ubc_to_node(struct ub_bus_controller *ubc);

#endif /* __UBUS_CONTROLLER_H__  */

/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) HiSilicon Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef __UBUS_CONTROLLER_H__
#define __UBUS_CONTROLLER_H__

struct ub_bus_controller;
struct ub_bus_controller_ops {
	int (*eu_table_init)(struct ub_bus_controller *ubc);
	void (*eu_table_uninit)(struct ub_bus_controller *ubc);
	int (*eu_cfg)(struct ub_bus_controller *ubc, bool flag, u32 eid, u16 upi);
	void (*register_decoder_base_addr)(struct ub_bus_controller *ubc,
					   u64 *cmd_queue, u64 *event_queue);
	int (*entity_enable)(struct ub_entity *uent, u8 enable);

	KABI_RESERVE(1)
	KABI_RESERVE(2)
	KABI_RESERVE(3)
	KABI_RESERVE(4)
	KABI_RESERVE(5)
	KABI_RESERVE(6)
	KABI_RESERVE(7)
	KABI_RESERVE(8)
};

struct ub_bus_controller *ub_find_bus_controller_by_cna(u32 cna);
void ub_bus_controllers_remove(void);
int ub_bus_controllers_probe(void);
int ub_ubc_to_node(struct ub_bus_controller *ubc);

#endif /* __UBUS_CONTROLLER_H__  */

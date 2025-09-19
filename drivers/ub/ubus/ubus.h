/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) HiSilicon Technologies Co., Ltd. 2025. All rights reserved.
 */
#ifndef __UBUS_H__
#define __UBUS_H__

#include <ub/ubfi/ubfi.h>
#include <ub/ubus/ubus.h>

#define UB_CP_UPI 0x7FFF

enum ub_entity_type {
	UB_ENT_BUS_CONTROLLER,
	UB_ENT_IBUS_CONTROLLER,
	UB_ENT_SWITCH,
	UB_ENT_DEVICE,
	UB_ENT_IDEVICE,
	UB_ENT_P_DEVICE,
	UB_ENT_P_IDEVICE,
	UB_ENT_UNKNOWN
};

#define is_primary(uent) ((uent)->entity_idx == 0)
#define is_ibus_controller(uent) ((uent)->ent_type == UB_ENT_IBUS_CONTROLLER)
#define is_bus_controller(uent) ((uent)->ent_type == UB_ENT_BUS_CONTROLLER)
#define is_controller(uent) (is_ibus_controller(uent) || is_bus_controller(uent))
#define is_switch(uent) ((uent)->ent_type == UB_ENT_SWITCH)
#define is_device(uent) ((uent)->ent_type == UB_ENT_DEVICE)
#define is_p_device(uent) ((uent)->ent_type == UB_ENT_P_DEVICE)
#define is_dev(uent) (is_device(uent) || is_p_device(uent))
#define is_idevice(uent) ((uent)->ent_type == UB_ENT_IDEVICE)
#define is_p_idevice(uent) ((uent)->ent_type == UB_ENT_P_IDEVICE)
#define is_idev(uent) (is_idevice(uent) || is_p_idevice(uent))

int ub_host_probe(void);
void ub_host_remove(void);

struct ub_manage_subsystem_ops {
	u32 vendor;
	int (*controller_probe)(struct ub_bus_controller *ubc);
	void (*controller_remove)(struct ub_bus_controller *ubc);
};
int register_ub_manage_subsystem_ops(const struct ub_manage_subsystem_ops *ops);
void unregister_ub_manage_subsystem_ops(const struct ub_manage_subsystem_ops *ops);
const struct ub_manage_subsystem_ops *get_ub_manage_subsystem_ops(void);

#endif /* __UBUS_H__ */

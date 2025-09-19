/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) HiSilicon Technologies Co., Ltd. 2025. All rights reserved.
 */
#ifndef __UBUS_H__
#define __UBUS_H__

#include <ub/ubfi/ubfi.h>
#include <ub/ubus/ubus.h>

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

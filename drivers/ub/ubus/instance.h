/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) HiSilicon Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef __INSTANCE_H__
#define __INSTANCE_H__

#include <linux/ummu_core.h>

typedef bool (*instance_match)(struct ub_bus_instance *bi, void *arg);

#define UBUS_INSTANCE_STATIC_SERVER 0

#define is_static_server(bi) ((bi)->info.type == UBUS_INSTANCE_STATIC_SERVER)
#define is_static(bi) (is_static_server(bi))
#define is_server(bi) (is_static_server(bi))

int ub_static_bus_instance_init(struct ub_bus_controller *ubc);
void ub_static_bus_instance_uninit(struct ub_bus_controller *ubc);
void ub_bus_instance_put(struct ub_bus_instance *bi);
struct ub_bus_instance *ub_find_bus_instance(instance_match match, void *arg);

#endif /* __INSTANCE_H__ */

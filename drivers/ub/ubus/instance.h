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
#define is_dynamic_server(bi) ((bi)->info.type == UBUS_INSTANCE_DYNAMIC_SERVER)
#define is_dynamic_cluster(bi) ((bi)->info.type == UBUS_INSTANCE_DYNAMIC_CLUSTER)
#define is_static(bi) (is_static_server(bi))
#define is_dynamic(bi) (is_dynamic_server(bi) || is_dynamic_cluster(bi))
#define is_server(bi) (is_static_server(bi) || is_dynamic_server(bi))
#define is_cluster(bi) (is_dynamic_cluster(bi))

int ub_static_bus_instance_init(struct ub_bus_controller *ubc);
void ub_static_bus_instance_uninit(struct ub_bus_controller *ubc);
void ub_bus_instance_put(struct ub_bus_instance *bi);
struct ub_bus_instance *ub_find_bus_instance(instance_match match, void *arg);
int ub_ioctl_bus_instance_create(void __user *uptr);
int ub_ioctl_bus_instance_destroy(void __user *uptr);
int ub_msg_bus_instance_create(struct ub_bus_controller *ubc, u32 *guid, u32 eid,
			       u16 upi, enum eid_type type);
int ub_msg_bus_instance_destroy(struct ub_bus_controller *ubc, u32 *guid);
void ub_dynamic_bus_instance_drain(void);

#endif /* __INSTANCE_H__ */

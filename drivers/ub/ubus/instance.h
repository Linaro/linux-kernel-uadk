/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) HiSilicon Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef __INSTANCE_H__
#define __INSTANCE_H__

#include <linux/ummu_core.h>

extern struct bus_attribute bus_attr_instance;

typedef bool (*instance_match)(struct ub_bus_instance *bi, void *arg);

#define UBUS_INSTANCE_STATIC_SERVER 0
#define UBUS_INSTANCE_STATIC_CLUSTER 1

#define is_static_server(bi) ((bi)->info.type == UBUS_INSTANCE_STATIC_SERVER)
#define is_static_cluster(bi) ((bi)->info.type == UBUS_INSTANCE_STATIC_CLUSTER)
#define is_dynamic_server(bi) ((bi)->info.type == UBUS_INSTANCE_DYNAMIC_SERVER)
#define is_dynamic_cluster(bi) ((bi)->info.type == UBUS_INSTANCE_DYNAMIC_CLUSTER)
#define is_static(bi) (is_static_server(bi) || is_static_cluster(bi))
#define is_dynamic(bi) (is_dynamic_server(bi) || is_dynamic_cluster(bi))
#define is_server(bi) (is_static_server(bi) || is_dynamic_server(bi))
#define is_cluster(bi) (is_static_cluster(bi) || is_dynamic_cluster(bi))

int ub_static_bus_instance_init(struct ub_bus_controller *ubc);
void ub_static_bus_instance_uninit(struct ub_bus_controller *ubc);
void ub_bus_instance_put(struct ub_bus_instance *bi);
struct ub_bus_instance *ub_find_bus_instance(instance_match match, void *arg);
int ub_ioctl_bus_instance_create(void __user *uptr);
int ub_ioctl_bus_instance_destroy(void __user *uptr);
int ub_ioctl_bus_instance_bind(void __user *uptr);
int ub_ioctl_bus_instance_unbind(void __user *uptr);
int ub_msg_bus_instance_create(struct ub_bus_controller *ubc, u32 *guid, u32 eid,
			       u16 upi, enum eid_type type);
int ub_msg_bus_instance_destroy(struct ub_bus_controller *ubc, u32 *guid);
void ub_dynamic_bus_instance_drain(void);
int ub_notify_bus_instance_handle(struct ub_bus_controller *ubc, bool flag,
				  u32 *guid, u32 eid, u16 upi);
void ub_static_cluster_instance_drain(void);
int ub_default_bus_instance_init(struct ub_entity *uent);
void ub_default_bus_instance_uninit(struct ub_entity *uent);

#endif /* __INSTANCE_H__ */

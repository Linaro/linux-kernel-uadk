/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) HiSilicon Technologies Co., Ltd. 2025. All rights reserved.
 */
#ifndef __SYSFS_H__
#define __SYSFS_H__

struct ub_entity;
extern const struct attribute_group *ub_entity_groups[];
extern const struct attribute_group *ub_bus_groups[];
extern const struct device_type ub_dev_type;
int ub_create_sysfs_dev_files(struct ub_entity *pue);
void ub_remove_sysfs_ent_files(struct ub_entity *pue);
int ub_bus_attr_dynamic_init(void);
void ub_bus_attr_dynamic_uninit(void);

#endif /* __SYSFS_H__ */

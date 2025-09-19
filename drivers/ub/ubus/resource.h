/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) HiSilicon Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef __RESOURCE_H__
#define __RESOURCE_H__

struct ub_entity;
int _ub_entity_setup_mmio(struct ub_entity *uent);
int ub_entity_setup_mmio(struct ub_entity *dev);
void ub_entity_unset_mmio(struct ub_entity *dev);
int ub_mmap_resource_range(struct ub_entity *uent, unsigned long idx,
			   struct vm_area_struct *vma, int write_combine);
int ub_insert_resource(struct ub_entity *dev, int idx);
void ub_entity_free_mmio_idx(struct ub_entity *dev, int idx);

#endif /* __RESOURCE_H__ */

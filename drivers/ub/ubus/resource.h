/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) HiSilicon Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef __RESOURCE_H__
#define __RESOURCE_H__

int ub_mmap_resource_range(struct ub_entity *uent, unsigned long idx,
			   struct vm_area_struct *vma, int write_combine);
void ub_entity_free_mmio_idx(struct ub_entity *dev, int idx);

#endif /* __RESOURCE_H__ */

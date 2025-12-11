/* SPDX-License-Identifier: GPL-2.0+ */
/* Copyright(c) 2025 HiSilicon Technologies CO., Ltd. All rights reserved. */

#ifndef __UDMA_CTX_H__
#define __UDMA_CTX_H__

#include <linux/ummu_core.h>
#include <ub/urma/ubcore_api.h>
#include "udma_dev.h"

struct udma_context {
	struct ubcore_ucontext base;
	struct udma_dev *dev;
	uint32_t uasid;
	struct list_head pgdir_list;
	struct mutex pgdir_mutex;
	struct iommu_sva *sva;
	uint32_t tid;
};

static inline struct udma_context *to_udma_context(struct ubcore_ucontext *uctx)
{
	return container_of(uctx, struct udma_context, base);
}

static inline uint64_t get_mmap_idx(struct vm_area_struct *vma)
{
	return (vma->vm_pgoff >> MAP_INDEX_SHIFT) & MAP_INDEX_MASK;
}

static inline int get_mmap_cmd(struct vm_area_struct *vma)
{
	return (vma->vm_pgoff & MAP_COMMAND_MASK);
}

struct ubcore_ucontext *udma_alloc_ucontext(struct ubcore_device *ub_dev,
					    uint32_t eid_index,
					    struct ubcore_udrv_priv *udrv_data);
int udma_free_ucontext(struct ubcore_ucontext *ucontext);
int udma_mmap(struct ubcore_ucontext *uctx, struct vm_area_struct *vma);

#endif /* __UDMA_CTX_H__ */

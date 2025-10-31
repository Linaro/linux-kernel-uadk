/* SPDX-License-Identifier: GPL-2.0+ */
/* Copyright(c) 2025 HiSilicon Technologies CO., Ltd. All rights reserved. */

#ifndef __UDMA_COMM_H__
#define __UDMA_COMM_H__

#include <linux/jhash.h>
#include <ub/urma/ubcore_api.h>
#include "udma_dev.h"

struct udma_umem_param {
	struct ubcore_device *ub_dev;
	uint64_t va;
	uint64_t len;
	union ubcore_umem_flag flag;
	bool is_kernel;
};

void udma_init_udma_table(struct udma_table *table, uint32_t max, uint32_t min);
void udma_init_udma_table_mutex(struct xarray *table, struct mutex *udma_mutex);
void udma_destroy_udma_table(struct udma_dev *dev, struct udma_table *table,
			     const char *table_name);
void udma_destroy_eid_table(struct udma_dev *udma_dev);
void *udma_alloc_iova(struct udma_dev *udma_dev, size_t memory_size, dma_addr_t *addr);
void udma_free_iova(struct udma_dev *udma_dev, size_t memory_size, void *kva_or_slot,
		    dma_addr_t addr);

#endif /* __UDMA_COMM_H__ */

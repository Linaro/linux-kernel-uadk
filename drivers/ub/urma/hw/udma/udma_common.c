// SPDX-License-Identifier: GPL-2.0+
/* Copyright(c) 2025 HiSilicon Technologies CO., Ltd. All rights reserved. */

#define dev_fmt(fmt) "UDMA: " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/iommu.h>
#include <linux/slab.h>
#include <linux/dma-mapping.h>
#include <linux/dmapool.h>
#include <linux/highmem.h>
#include <ub/ubase/ubase_comm_dev.h>
#include <ub/ubase/ubase_comm_cmd.h>
#include "udma_dev.h"
#include "udma_cmd.h"
#include "udma_common.h"

static void udma_init_ida_table(struct udma_ida *ida_table, uint32_t max, uint32_t min)
{
	ida_init(&ida_table->ida);
	spin_lock_init(&ida_table->lock);
	ida_table->max = max;
	ida_table->min = min;
	ida_table->next = min;
}

void udma_init_udma_table(struct udma_table *table, uint32_t max, uint32_t min)
{
	udma_init_ida_table(&table->ida_table, max, min);
	xa_init(&table->xa);
}

void udma_init_udma_table_mutex(struct xarray *table, struct mutex *udma_mutex)
{
	xa_init(table);
	mutex_init(udma_mutex);
}

void udma_destroy_udma_table(struct udma_dev *dev, struct udma_table *table,
				    const char *table_name)
{
	if (!ida_is_empty(&table->ida_table.ida))
		dev_err(dev->dev, "IDA not empty in clean up %s table.\n",
			table_name);
	ida_destroy(&table->ida_table.ida);

	if (!xa_empty(&table->xa))
		dev_err(dev->dev, "%s not empty.\n", table_name);
	xa_destroy(&table->xa);
}

static void udma_clear_eid_table(struct udma_dev *udma_dev)
{
	struct udma_ctrlq_eid_info *eid_entry = NULL;
	unsigned long index = 0;
	eid_t ummu_eid = 0;
	guid_t guid = {};

	if (!xa_empty(&udma_dev->eid_table)) {
		xa_for_each(&udma_dev->eid_table, index, eid_entry) {
			xa_erase(&udma_dev->eid_table, index);
			if (!udma_dev->is_ue) {
				(void)memcpy(&ummu_eid, eid_entry->eid.raw, sizeof(ummu_eid));
				ummu_core_del_eid(&guid, ummu_eid, EID_NONE);
			}
			kfree(eid_entry);
			eid_entry = NULL;
		}
	}
}

void udma_destroy_eid_table(struct udma_dev *udma_dev)
{
	udma_clear_eid_table(udma_dev);
	xa_destroy(&udma_dev->eid_table);
	mutex_destroy(&udma_dev->eid_mutex);
}

void *udma_alloc_iova(struct udma_dev *udma_dev, size_t memory_size, dma_addr_t *addr)
{
	struct iova_slot *slot;
	uint32_t npage;
	size_t sizep;
	int ret;

	slot = dma_alloc_iova(udma_dev->dev, memory_size, 0, addr, &sizep);
	if (IS_ERR_OR_NULL(slot)) {
		dev_err(udma_dev->dev,
			"failed to dma alloc iova, size = %lu, ret = %ld.\n",
			memory_size, PTR_ERR(slot));
		return NULL;
	}

	npage = sizep >> PAGE_SHIFT;
	ret = ummu_fill_pages(slot, *addr, npage);
	if (ret) {
		dev_err(udma_dev->dev,
			"ummu fill pages failed, npage = %u, ret = %d", npage, ret);
		dma_free_iova(slot);
		return NULL;
	}

	return (void *)slot;
}

void udma_free_iova(struct udma_dev *udma_dev, size_t memory_size, void *kva_or_slot,
		    dma_addr_t addr)
{
	size_t aligned_memory_size;
	struct iova_slot *slot;
	uint32_t npage;
	int ret;

	aligned_memory_size = PAGE_ALIGN(memory_size);
	npage = aligned_memory_size >> PAGE_SHIFT;
	slot = (struct iova_slot *)kva_or_slot;
	ret = ummu_drain_pages(slot, addr, npage);
	if (ret)
		dev_err(udma_dev->dev,
			"ummu drain pages failed, npage = %u, ret = %d.\n",
			npage, ret);

	dma_free_iova(slot);
}

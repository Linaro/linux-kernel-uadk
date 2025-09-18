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
#include <uapi/ub/urma/udma/udma_abi.h>
#include "udma_dev.h"
#include "udma_cmd.h"
#include "udma_common.h"

static int udma_verify_input(struct udma_umem_param *param)
{
	struct udma_dev *udma_dev = to_udma_dev(param->ub_dev);

	if (((param->va + param->len) < param->va) ||
		PAGE_ALIGN(param->va + param->len) < (param->va + param->len)) {
		dev_err(udma_dev->dev, "invalid pin_page param, len=%llu.\n",
			param->len);
		return -EINVAL;
	}
	return 0;
}

static void udma_fill_umem(struct ubcore_umem *umem, struct udma_umem_param *param)
{
	umem->ub_dev = param->ub_dev;
	umem->va = param->va;
	umem->length = param->len;
	umem->flag = param->flag;
}

static struct scatterlist *udma_sg_set_page(struct scatterlist *sg_start,
					    int pinned, struct page **page_list)
{
	struct scatterlist *sg;
	int i;

	for_each_sg(sg_start, sg, pinned, i)
		sg_set_page(sg, page_list[i], PAGE_SIZE, 0);

	return sg;
}

static int udma_pin_pages(uint64_t cur_base, uint64_t npages,
			  uint32_t gup_flags, struct page **page_list)
{
	return pin_user_pages_fast(cur_base, min_t(unsigned long, (unsigned long)npages,
				   PAGE_SIZE / sizeof(struct page *)),
				   gup_flags | FOLL_LONGTERM, page_list);
}

static uint64_t udma_pin_all_pages(struct udma_dev *udma_dev, struct ubcore_umem *umem,
				   uint64_t npages, uint32_t gup_flags,
				   struct page **page_list)
{
	struct scatterlist *sg_list_start = umem->sg_head.sgl;
	uint64_t cur_base = umem->va & PAGE_MASK;
	uint64_t page_count = npages;
	int pinned;

	while (page_count != 0) {
		cond_resched();
		pinned = udma_pin_pages(cur_base, page_count, gup_flags, page_list);
		if (pinned <= 0) {
			dev_err(udma_dev->dev, "failed to pin_user_pages_fast, page_count: %llu, pinned: %d.\n",
				page_count, pinned);
			return npages - page_count;
		}
		cur_base += (uint64_t)pinned * PAGE_SIZE;
		page_count -= (uint64_t)pinned;
		sg_list_start = udma_sg_set_page(sg_list_start, pinned, page_list);
	}
	return npages;
}

static uint64_t udma_k_pin_pages(struct udma_dev *dev, struct ubcore_umem *umem,
				 uint64_t npages)
{
	struct scatterlist *sg_cur = umem->sg_head.sgl;
	uint64_t cur_base = umem->va & PAGE_MASK;
	struct page *pg;
	uint64_t pinned;

	for (pinned = 0; pinned < npages; pinned++) {
		if (is_vmalloc_addr((void *)(uintptr_t)cur_base))
			pg = vmalloc_to_page((void *)(uintptr_t)cur_base);
		else
			pg = kmap_to_page((void *)(uintptr_t)cur_base);
		if (!pg) {
			dev_err(dev->dev, "vmalloc or kmap to page failed.\n");
			break;
		}
		get_page(pg);

		cur_base += PAGE_SIZE;

		sg_set_page(sg_cur, pg, PAGE_SIZE, 0);
		sg_cur = sg_next(sg_cur);
	}

	return pinned;
}

static void udma_unpin_pages(struct ubcore_umem *umem, uint64_t nents, bool is_kernel)
{
	struct scatterlist *sg;
	uint32_t i;

	for_each_sg(umem->sg_head.sgl, sg, nents, i) {
		struct page *page = sg_page(sg);

		if (is_kernel)
			put_page(page);
		else
			unpin_user_page(page);
	}
}

static struct ubcore_umem *udma_get_target_umem(struct udma_umem_param *param,
						struct page **page_list)
{
	struct udma_dev *udma_dev = to_udma_dev(param->ub_dev);
	struct ubcore_umem *umem;
	uint32_t gup_flags;
	uint64_t npages;
	uint64_t pinned;
	int ret = 0;

	umem = kzalloc(sizeof(*umem), GFP_KERNEL);
	if (umem == 0) {
		ret = -ENOMEM;
		goto out;
	}

	udma_fill_umem(umem, param);

	npages = udma_cal_npages(umem->va, umem->length);
	if (npages == 0 || npages > UINT_MAX) {
		dev_err(udma_dev->dev,
			"Invalid npages %llu in getting target umem process.\n", npages);
		ret = -EINVAL;
		goto umem_kfree;
	}

	ret = sg_alloc_table(&umem->sg_head, (unsigned int)npages, GFP_KERNEL);
	if (ret)
		goto umem_kfree;

	if (param->is_kernel) {
		pinned = udma_k_pin_pages(udma_dev, umem, npages);
	} else {
		gup_flags = (param->flag.bs.writable == 1) ? FOLL_WRITE : 0;
		pinned = udma_pin_all_pages(udma_dev, umem, npages, gup_flags, page_list);
	}
	if (pinned != npages) {
		ret = -ENOMEM;
		goto umem_release;
	}

	goto out;

umem_release:
	udma_unpin_pages(umem, pinned, param->is_kernel);
	sg_free_table(&umem->sg_head);
umem_kfree:
	kfree(umem);
out:
	return ret != 0 ? ERR_PTR(ret) : umem;
}

struct ubcore_umem *udma_umem_get(struct udma_umem_param *param)
{
	struct ubcore_umem *umem;
	struct page **page_list;
	int ret;

	ret = udma_verify_input(param);
	if (ret < 0)
		return ERR_PTR(ret);

	page_list = (struct page **) __get_free_page(GFP_KERNEL);
	if (page_list == 0)
		return ERR_PTR(-ENOMEM);

	umem = udma_get_target_umem(param, page_list);

	free_page((uintptr_t)page_list);

	return umem;
}

int pin_queue_addr(struct udma_dev *dev, uint64_t addr, uint32_t len,
		   struct udma_buf *buf)
{
	struct ubcore_device *ub_dev = &dev->ub_dev;
	struct udma_umem_param param;

	param.ub_dev = ub_dev;
	param.va = addr;
	param.len = len;
	param.flag.bs.writable = 1;
	param.flag.bs.non_pin = 0;
	param.is_kernel = false;

	buf->umem = udma_umem_get(&param);
	if (IS_ERR(buf->umem)) {
		dev_err(dev->dev, "failed to pin queue addr.\n");
		return PTR_ERR(buf->umem);
	}

	buf->addr = addr;

	return 0;
}

void unpin_queue_addr(struct ubcore_umem *umem)
{
	udma_umem_release(umem, false);
}

void udma_umem_release(struct ubcore_umem *umem, bool is_kernel)
{
	if (IS_ERR_OR_NULL(umem))
		return;

	udma_unpin_pages(umem, umem->sg_head.nents, is_kernel);
	sg_free_table(&umem->sg_head);
	kfree(umem);
}

int udma_id_alloc_auto_grow(struct udma_dev *udma_dev, struct udma_ida *ida_table,
			    uint32_t *idx)
{
	int id;

	spin_lock(&ida_table->lock);
	id = ida_alloc_range(&ida_table->ida, ida_table->next, ida_table->max,
			     GFP_ATOMIC);
	if (id < 0) {
		id = ida_alloc_range(&ida_table->ida, ida_table->min, ida_table->max,
				     GFP_ATOMIC);
		if (id < 0) {
			dev_err(udma_dev->dev, "failed to alloc id, ret = %d.\n", id);
			spin_unlock(&ida_table->lock);
			return id;
		}
	}

	ida_table->next = (uint32_t)id + 1 > ida_table->max ?
			  ida_table->min : (uint32_t)id + 1;

	*idx = (uint32_t)id;
	spin_unlock(&ida_table->lock);

	return 0;
}

int udma_id_alloc(struct udma_dev *udma_dev, struct udma_ida *ida_table,
		  uint32_t *idx)
{
	int id;

	id = ida_alloc_range(&ida_table->ida, ida_table->min, ida_table->max,
			     GFP_ATOMIC);
	if (id < 0) {
		dev_err(udma_dev->dev, "failed to alloc id, ret = %d.\n", id);
		return id;
	}

	*idx = (uint32_t)id;

	return 0;
}

int udma_specify_adv_id(struct udma_dev *udma_dev, struct udma_group_bitmap *bitmap_table,
			uint32_t user_id)
{
	uint32_t id_bit_idx = (user_id - bitmap_table->min);
	uint32_t bit_idx = id_bit_idx % NUM_JETTY_PER_GROUP;
	uint32_t block = id_bit_idx / NUM_JETTY_PER_GROUP;
	uint32_t *bit = bitmap_table->bit;

	spin_lock(&bitmap_table->lock);
	if ((bit[block] & (1U << bit_idx)) == 0) {
		dev_err(udma_dev->dev,
			"user specify id %u been used.\n", user_id);
		spin_unlock(&bitmap_table->lock);
		return -ENOMEM;
	}

	bit[block] &= ~(1U << bit_idx);
	spin_unlock(&bitmap_table->lock);

	return 0;
}

static int udma_adv_jetty_id_alloc(struct udma_dev *udma_dev, uint32_t *bit,
				   uint32_t next_bit, uint32_t start_idx,
				   struct udma_group_bitmap *bitmap_table)
{
	uint32_t bit_idx;

	bit_idx = find_next_bit((unsigned long *)bit, NUM_JETTY_PER_GROUP, next_bit);
	if (bit_idx == NUM_JETTY_PER_GROUP) {
		dev_err(udma_dev->dev,
			"jid is larger than n_bits, bit=0x%x.\n", *bit);
		return -ENOMEM;
	}

	start_idx += bit_idx;
	if (start_idx >= bitmap_table->n_bits) {
		dev_err(udma_dev->dev,
			"jid is larger than n_bits, id=%u, n_bits=%u.\n",
			start_idx, bitmap_table->n_bits);
		return -ENOMEM;
	}

	*bit &= ~(1U << bit_idx);
	return start_idx + bitmap_table->min;
}

int udma_adv_id_alloc(struct udma_dev *udma_dev, struct udma_group_bitmap *bitmap_table,
		      uint32_t *start_idx, bool is_grp, uint32_t next)
{
	uint32_t next_block = (next - bitmap_table->min) / NUM_JETTY_PER_GROUP;
	uint32_t next_bit = (next - bitmap_table->min) % NUM_JETTY_PER_GROUP;
	uint32_t bitmap_cnt = bitmap_table->bitmap_cnt;
	uint32_t *bit = bitmap_table->bit;
	uint32_t i;
	int ret;

	spin_lock(&bitmap_table->lock);

	for (i = next_block;
	     (i < bitmap_cnt && bit[i] == 0) ||
	     (i == next_block &&
	      ((bit[i] & GENMASK(NUM_JETTY_PER_GROUP - 1, next_bit)) == 0)); ++i)
		;

	if (i == bitmap_cnt) {
		dev_err(udma_dev->dev,
			"all bitmaps have been used, bitmap_cnt = %u.\n",
			bitmap_cnt);
		spin_unlock(&bitmap_table->lock);
		return -ENOMEM;
	}

	if (!is_grp) {
		ret = udma_adv_jetty_id_alloc(udma_dev, bit + i, next_bit,
					      i * NUM_JETTY_PER_GROUP, bitmap_table);

		spin_unlock(&bitmap_table->lock);
		if (ret >= 0) {
			*start_idx = (uint32_t)ret;
			return 0;
		}
		return ret;
	}

	for (; i < bitmap_cnt && ~bit[i] != 0; ++i)
		;
	if (i == bitmap_cnt ||
	    (i + 1) * NUM_JETTY_PER_GROUP > bitmap_table->n_bits) {
		dev_err(udma_dev->dev,
			"no completely bitmap for Jetty group.\n");
		spin_unlock(&bitmap_table->lock);
		return -ENOMEM;
	}

	bit[i] = 0;
	*start_idx = i * NUM_JETTY_PER_GROUP + bitmap_table->min;

	spin_unlock(&bitmap_table->lock);

	return 0;
}

void udma_adv_id_free(struct udma_group_bitmap *bitmap_table, uint32_t start_idx,
		      bool is_grp)
{
	uint32_t bitmap_num;
	uint32_t bit_num;

	start_idx -= bitmap_table->min;
	spin_lock(&bitmap_table->lock);

	bitmap_num = start_idx / NUM_JETTY_PER_GROUP;
	if (bitmap_num >= bitmap_table->bitmap_cnt) {
		spin_unlock(&bitmap_table->lock);
		return;
	}

	if (is_grp) {
		bitmap_table->bit[bitmap_num] = ~0U;
	} else {
		bit_num = start_idx % NUM_JETTY_PER_GROUP;
		bitmap_table->bit[bitmap_num] |= (1U << bit_num);
	}

	spin_unlock(&bitmap_table->lock);
}

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

static struct ubcore_umem *udma_pin_k_addr(struct ubcore_device *ub_dev, uint64_t va,
				    uint64_t len)
{
	struct udma_umem_param param;

	param.ub_dev = ub_dev;
	param.va = va;
	param.len = len;
	param.flag.bs.writable = true;
	param.flag.bs.non_pin = 0;
	param.is_kernel = true;

	return udma_umem_get(&param);
}

static void udma_unpin_k_addr(struct ubcore_umem *umem)
{
	udma_umem_release(umem, true);
}

int udma_k_alloc_buf(struct udma_dev *udma_dev, size_t memory_size,
			  struct udma_buf *buf)
{
	size_t aligned_memory_size;
	int ret;

	aligned_memory_size = memory_size + UDMA_HW_PAGE_SIZE - 1;
	buf->aligned_va = vmalloc(aligned_memory_size);
	if (!buf->aligned_va) {
		dev_err(udma_dev->dev,
			"failed to vmalloc kernel buf, size = %lu.",
			aligned_memory_size);
		return -ENOMEM;
	}

	memset(buf->aligned_va, 0, aligned_memory_size);
	buf->umem = udma_pin_k_addr(&udma_dev->ub_dev, (uint64_t)buf->aligned_va,
				    aligned_memory_size);
	if (IS_ERR(buf->umem)) {
		ret = PTR_ERR(buf->umem);
		vfree(buf->aligned_va);
		dev_err(udma_dev->dev, "pin kernel buf failed, ret = %d.\n", ret);
		return ret;
	}

	buf->addr = ((uint64_t)buf->aligned_va + UDMA_HW_PAGE_SIZE - 1) &
		    ~(UDMA_HW_PAGE_SIZE - 1);
	buf->kva = (void *)(uintptr_t)buf->addr;

	return 0;
}

void udma_k_free_buf(struct udma_dev *udma_dev, size_t memory_size,
		     struct udma_buf *buf)
{
	udma_unpin_k_addr(buf->umem);
	vfree(buf->aligned_va);
	buf->aligned_va = NULL;
	buf->kva = NULL;
	buf->addr = 0;
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

void udma_swap_endian(uint8_t arr[], uint8_t res[], uint32_t res_size)
{
	uint32_t i;

	for (i = 0; i < res_size; i++)
		res[i] = arr[res_size - i - 1];
}

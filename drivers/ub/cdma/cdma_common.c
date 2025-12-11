// SPDX-License-Identifier: GPL-2.0+
/* Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved. */

#define dev_fmt(fmt) "CDMA: " fmt

#include <linux/highmem.h>
#include <linux/scatterlist.h>
#include <linux/mm.h>
#include "cdma_common.h"
#include "cdma.h"

static inline void cdma_fill_umem(struct cdma_umem *umem,
				  struct cdma_umem_param *param)
{
	umem->dev = param->dev;
	umem->va = param->va;
	umem->length = param->len;
	umem->flag = param->flag;
}

static int cdma_pin_part_of_pages(u64 cur_base, u64 npages, u32 gup_flags,
				  struct page **page_list)
{
	/*
	 * page_list size is 4kB, the nr_pages should not larger than
	 * PAGE_SIZE / sizeof(struct page *)
	 */
	return pin_user_pages_fast(cur_base,
				   min_t(unsigned long, (unsigned long)npages,
					 PAGE_SIZE / sizeof(struct page *)),
				   gup_flags | FOLL_LONGTERM, page_list);
}

static struct scatterlist *cdma_sg_set_page(struct scatterlist *sg_start,
					    int pinned, struct page **page_list)
{
	struct scatterlist *sg;
	int i;

	for_each_sg(sg_start, sg, pinned, i)
		sg_set_page(sg, page_list[i], PAGE_SIZE, 0);

	return sg;
}

static u64 cdma_pin_pages(struct cdma_dev *cdev, struct cdma_umem *umem,
			  u64 npages, u32 gup_flags, struct page **pages)
{
	struct scatterlist *sg_list_start = umem->sg_head.sgl;
	u64 cur_base = umem->va & PAGE_MASK;
	u64 page_count = npages;
	int pinned;

	while (page_count > 0) {
		cond_resched();

		pinned = cdma_pin_part_of_pages(cur_base, page_count, gup_flags,
						pages);
		if (pinned <= 0) {
			dev_err(cdev->dev,
				"pin pages failed, page_count = 0x%llx, pinned = %d.\n",
				page_count, pinned);
			return npages - page_count;
		}
		cur_base += (u64)pinned * PAGE_SIZE;
		page_count -= (u64)pinned;
		sg_list_start = cdma_sg_set_page(sg_list_start, pinned, pages);
	}

	return npages;
}

static u64 cdma_k_pin_pages(struct cdma_dev *cdev, struct cdma_umem *umem,
			    u64 npages)
{
	struct scatterlist *sg_cur = umem->sg_head.sgl;
	u64 cur_base = umem->va & PAGE_MASK;
	struct page *pg;
	u64 n;

	for (n = 0; n < npages; n++) {
		if (is_vmalloc_addr((void *)(uintptr_t)cur_base))
			pg = vmalloc_to_page((void *)(uintptr_t)cur_base);
		else
			pg = kmap_to_page((void *)(uintptr_t)cur_base);

		if (!pg) {
			dev_err(cdev->dev, "vmalloc or kmap to page failed.\n");
			break;
		}

		get_page(pg);

		cur_base += PAGE_SIZE;

		sg_set_page(sg_cur, pg, PAGE_SIZE, 0);
		sg_cur = sg_next(sg_cur);
	}

	return n;
}

static void cdma_unpin_pages(struct cdma_umem *umem, u64 nents, bool is_kernel)
{
	struct scatterlist *sg;
	struct page *page;
	u32 i;

	for_each_sg(umem->sg_head.sgl, sg, nents, i) {
		page = sg_page(sg);

		if (is_kernel)
			put_page(page);
		else
			unpin_user_page(page);
	}
}

static struct cdma_umem *cdma_get_target_umem(struct cdma_umem_param *param,
					      struct page **page_list)
{
	struct cdma_dev *cdev = param->dev;
	struct cdma_umem *umem;
	u64 npages, pinned;
	u32 gup_flags;
	int ret = 0;

	umem = kzalloc(sizeof(*umem), GFP_KERNEL);
	if (!umem) {
		ret = -ENOMEM;
		goto out;
	}

	cdma_fill_umem(umem, param);

	npages = cdma_cal_npages(umem->va, umem->length);
	if (!npages || npages > UINT_MAX) {
		dev_err(cdev->dev,
			"invalid npages %llu in getting target umem process.\n", npages);
		ret = -EINVAL;
		goto umem_kfree;
	}

	ret = sg_alloc_table(&umem->sg_head, (unsigned int)npages, GFP_KERNEL);
	if (ret)
		goto umem_kfree;

	if (param->is_kernel) {
		pinned = cdma_k_pin_pages(cdev, umem, npages);
	} else {
		gup_flags = param->flag.bs.writable ? FOLL_WRITE : 0;
		pinned = cdma_pin_pages(cdev, umem, npages, gup_flags,
					page_list);
	}

	if (pinned != npages) {
		ret = -ENOMEM;
		goto umem_release;
	}

	goto out;

umem_release:
	cdma_unpin_pages(umem, pinned, param->is_kernel);
	sg_free_table(&umem->sg_head);
umem_kfree:
	kfree(umem);
out:
	return ret != 0 ? ERR_PTR(ret) : umem;
}

static int cdma_verify_input(struct cdma_dev *cdev, u64 va, u64 len)
{
	if (((va + len) <= va) || PAGE_ALIGN(va + len) < (va + len)) {
		dev_err(cdev->dev, "invalid address parameter, len = %llu.\n",
			len);
		return -EINVAL;
	}
	return 0;
}

struct cdma_umem *cdma_umem_get(struct cdma_dev *cdev, u64 va, u64 len,
				bool is_kernel)
{
	struct cdma_umem_param param;
	struct page **page_list;
	struct cdma_umem *umem;
	int ret;

	ret = cdma_verify_input(cdev, va, len);
	if (ret)
		return ERR_PTR(ret);

	page_list = (struct page **)__get_free_page(GFP_KERNEL);
	if (!page_list)
		return ERR_PTR(-ENOMEM);

	param.dev = cdev;
	param.va = va;
	param.len = len;
	param.flag.bs.writable = true;
	param.flag.bs.non_pin = 0;
	param.is_kernel = is_kernel;
	umem = cdma_get_target_umem(&param, page_list);
	if (IS_ERR(umem))
		dev_err(cdev->dev, "get target umem failed.\n");

	free_page((unsigned long)(uintptr_t)page_list);
	return umem;
}

void cdma_umem_release(struct cdma_umem *umem, bool is_kernel)
{
	if (IS_ERR_OR_NULL(umem))
		return;

	cdma_unpin_pages(umem, umem->sg_head.nents, is_kernel);
	sg_free_table(&umem->sg_head);
	kfree(umem);
}

int cdma_k_alloc_buf(struct cdma_dev *cdev, size_t memory_size,
			 struct cdma_buf *buf)
{
	size_t aligned_memory_size;
	int ret;

	aligned_memory_size = memory_size + CDMA_HW_PAGE_SIZE - 1;
	buf->aligned_va = vmalloc(aligned_memory_size);
	if (!buf->aligned_va) {
		dev_err(cdev->dev,
			"vmalloc kernel buf failed, size = %lu.\n",
			aligned_memory_size);
		return -ENOMEM;
	}

	memset(buf->aligned_va, 0, aligned_memory_size);
	buf->umem = cdma_umem_get(cdev, (u64)buf->aligned_va,
				  aligned_memory_size, true);
	if (IS_ERR(buf->umem)) {
		ret = PTR_ERR(buf->umem);
		vfree(buf->aligned_va);
		dev_err(cdev->dev, "pin kernel buf failed, ret = %d.\n", ret);
		return ret;
	}

	buf->addr = ((u64)buf->aligned_va + CDMA_HW_PAGE_SIZE - 1) &
			~(CDMA_HW_PAGE_SIZE - 1);
	buf->kva = (void *)buf->addr;

	return 0;
}

void cdma_k_free_buf(struct cdma_dev *cdev, size_t memory_size,
			 struct cdma_buf *buf)
{
	cdma_umem_release(buf->umem, true);
	vfree(buf->aligned_va);
	buf->aligned_va = NULL;
	buf->kva = NULL;
	buf->addr = 0;
}

int cdma_pin_queue_addr(struct cdma_dev *cdev, u64 addr, u32 len,
			struct cdma_buf *buf)
{
	int ret = 0;

	if (IS_ERR_OR_NULL(buf))
		return -EINVAL;

	buf->umem = cdma_umem_get(cdev, addr, len, false);
	if (IS_ERR(buf->umem)) {
		dev_err(cdev->dev, "get umem failed.\n");
		ret = PTR_ERR(buf->umem);
		return ret;
	}

	buf->addr = addr;

	return ret;
}

void cdma_unpin_queue_addr(struct cdma_umem *umem)
{
	cdma_umem_release(umem, false);
}

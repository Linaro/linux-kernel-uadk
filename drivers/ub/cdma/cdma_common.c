// SPDX-License-Identifier: GPL-2.0+
/* Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved. */

#define dev_fmt(fmt) "CDMA: " fmt

#include <linux/highmem.h>
#include <linux/scatterlist.h>
#include <linux/mm.h>
#include "cdma_common.h"
#include "cdma.h"

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

void cdma_umem_release(struct cdma_umem *umem, bool is_kernel)
{
	if (IS_ERR_OR_NULL(umem))
		return;

	cdma_unpin_pages(umem, umem->sg_head.nents, is_kernel);
	sg_free_table(&umem->sg_head);
	kfree(umem);
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

void cdma_unpin_queue_addr(struct cdma_umem *umem)
{
	cdma_umem_release(umem, false);
}

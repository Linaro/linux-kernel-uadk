// SPDX-License-Identifier: GPL-2.0+
/* Copyright(c) 2025 HiSilicon Technologies CO., Ltd. All rights reserved. */

#define dev_fmt(fmt) "UDMA: " fmt

#include <linux/bitmap.h>
#include <linux/dma-mapping.h>
#include "udma_common.h"
#include "udma_db.h"

int udma_pin_sw_db(struct udma_context *ctx, struct udma_sw_db *db)
{
	uint64_t page_addr = db->db_addr & PAGE_MASK;
	struct udma_sw_db_page *page;
	struct udma_umem_param param;
	uint32_t offset = 0;
	int ret = 0;

	param.ub_dev = &ctx->dev->ub_dev;
	param.va = page_addr;
	param.len = PAGE_SIZE;
	param.flag.bs.writable = 1;
	param.flag.bs.non_pin = 0;
	param.is_kernel = false;
	offset = db->db_addr - page_addr;

	mutex_lock(&ctx->pgdir_mutex);

	list_for_each_entry(page, &ctx->pgdir_list, list) {
		if (page->user_virt == page_addr)
			goto found;
	}

	page = kmalloc(sizeof(*page), GFP_KERNEL);
	if (!page) {
		ret = -ENOMEM;
		goto out;
	}

	refcount_set(&page->refcount, 1);
	page->user_virt = page_addr;
	page->umem = udma_umem_get(&param);
	if (IS_ERR(page->umem)) {
		ret = PTR_ERR(page->umem);
		dev_err(ctx->dev->dev, "Failed to get umem, ret: %d.\n", ret);
		kfree(page);
		goto out;
	}

	list_add(&page->list, &ctx->pgdir_list);
	db->page = page;
	db->virt_addr = (char *)sg_virt(page->umem->sg_head.sgl) + offset;
	mutex_unlock(&ctx->pgdir_mutex);
	return 0;
found:
	db->page = page;
	db->virt_addr = (char *)sg_virt(page->umem->sg_head.sgl) + offset;
	refcount_inc(&page->refcount);
out:
	mutex_unlock(&ctx->pgdir_mutex);
	return ret;
}

void udma_unpin_sw_db(struct udma_context *ctx, struct udma_sw_db *db)
{
	mutex_lock(&ctx->pgdir_mutex);

	if (refcount_dec_and_test(&db->page->refcount)) {
		list_del(&db->page->list);
		udma_umem_release(db->page->umem, false);
		kfree(db->page);
	}

	mutex_unlock(&ctx->pgdir_mutex);
}

static int udma_alloc_db_from_page(struct udma_k_sw_db_page *page,
				   struct udma_sw_db *db, enum udma_db_type type)
{
	uint32_t index;

	index = find_first_bit(page->bitmap, page->num_db);
	if (index >= page->num_db)
		return -ENOMEM;

	clear_bit(index, page->bitmap);

	db->index = index;
	db->kpage = page;
	db->type = type;
	db->db_addr = page->db_buf.addr + db->index * UDMA_DB_SIZE;
	db->db_record = page->db_buf.kva + db->index * UDMA_DB_SIZE;
	*db->db_record = 0;

	return 0;
}

static struct udma_k_sw_db_page *udma_alloc_db_page(struct udma_dev *dev,
						    enum udma_db_type type)
{
	struct udma_k_sw_db_page *page;
	int ret;

	page = kzalloc(sizeof(*page), GFP_KERNEL);
	if (!page)
		return NULL;

	page->num_db = PAGE_SIZE / UDMA_DB_SIZE;

	page->bitmap = bitmap_alloc(page->num_db, GFP_KERNEL);
	if (!page->bitmap) {
		dev_err(dev->dev, "Failed alloc db bitmap, db type is %u.\n", type);
		goto err_bitmap;
	}

	bitmap_fill(page->bitmap, page->num_db);

	ret = udma_k_alloc_buf(dev, PAGE_SIZE, &page->db_buf);
	if (ret) {
		dev_err(dev->dev, "Failed alloc db page buf, ret is %d.\n", ret);
		goto err_kva;
	}

	return page;
err_kva:
	bitmap_free(page->bitmap);
err_bitmap:
	kfree(page);
	return NULL;
}

int udma_alloc_sw_db(struct udma_dev *dev, struct udma_sw_db *db,
		     enum udma_db_type type)
{
	struct udma_k_sw_db_page *page;
	int ret = 0;

	mutex_lock(&dev->db_mutex);

	list_for_each_entry(page, &dev->db_list[type], list)
		if (!udma_alloc_db_from_page(page, db, type))
			goto out;

	page = udma_alloc_db_page(dev, type);
	if (!page) {
		ret = -ENOMEM;
		dev_err(dev->dev, "Failed alloc sw db page db_type = %u\n", type);
		goto out;
	}

	list_add(&page->list, &dev->db_list[type]);

	/* This should never fail */
	(void)udma_alloc_db_from_page(page, db, type);
out:
	mutex_unlock(&dev->db_mutex);

	return ret;
}

void udma_free_sw_db(struct udma_dev *dev, struct udma_sw_db *db)
{
	mutex_lock(&dev->db_mutex);

	set_bit(db->index, db->kpage->bitmap);

	if (bitmap_full(db->kpage->bitmap, db->kpage->num_db)) {
		udma_k_free_buf(dev, PAGE_SIZE, &db->kpage->db_buf);
		bitmap_free(db->kpage->bitmap);
		list_del(&db->kpage->list);
		kfree(db->kpage);
		db->kpage = NULL;
	}

	mutex_unlock(&dev->db_mutex);
}

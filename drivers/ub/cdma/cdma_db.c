// SPDX-License-Identifier: GPL-2.0+
/* Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved. */

#define dev_fmt(fmt) "CDMA: " fmt

#include <linux/bitmap.h>
#include "cdma_common.h"
#include "cdma_context.h"
#include "cdma_db.h"

static void cdma_free_db_page(struct cdma_dev *cdev, struct cdma_sw_db *db)
{
	cdma_k_free_buf(cdev, PAGE_SIZE, &db->kpage->db_buf);
	bitmap_free(db->kpage->bitmap);
	kfree(db->kpage);
	db->kpage = NULL;
}

void cdma_unpin_sw_db(struct cdma_context *ctx, struct cdma_sw_db *db)
{
	mutex_lock(&ctx->pgdir_mutex);

	if (refcount_dec_and_test(&db->page->refcount)) {
		list_del(&db->page->list);
		cdma_umem_release(db->page->umem, false);
		kfree(db->page);
		db->page = NULL;
	}

	mutex_unlock(&ctx->pgdir_mutex);
}

void cdma_free_sw_db(struct cdma_dev *cdev, struct cdma_sw_db *db)
{
	mutex_lock(&cdev->db_mutex);

	set_bit(db->index, db->kpage->bitmap);

	if (bitmap_full(db->kpage->bitmap, db->kpage->num_db)) {
		list_del(&db->kpage->list);
		cdma_free_db_page(cdev, db);
	}

	mutex_unlock(&cdev->db_mutex);
}

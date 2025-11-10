// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 *
 * Description: ubagg kernel module
 * Author: Weicheng Zhang
 * Create: 2025-8-6
 * Note:
 * History: 2025-8-6: Create file
 */

#include "ubagg_bitmap.h"
#include "ubagg_log.h"

struct ubagg_bitmap *ubagg_bitmap_alloc(uint32_t bitmap_size)
{
	struct ubagg_bitmap *bitmap;

	bitmap = kcalloc(1, sizeof(struct ubagg_bitmap), GFP_KERNEL);
	if (bitmap == NULL)
		return NULL;
	bitmap->size = bitmap_size;
	bitmap->bits = kcalloc(BITS_TO_LONGS(bitmap_size),
			       sizeof(unsigned long), GFP_KERNEL);
	if (bitmap->bits == NULL) {
		kfree(bitmap);
		return NULL;
	}
	bitmap->alloc_idx = 0;
	spin_lock_init(&bitmap->lock);
	return bitmap;
}

void ubagg_bitmap_free(struct ubagg_bitmap *bitmap)
{
	spin_lock(&bitmap->lock);
	if (bitmap->bits != NULL)
		kfree(bitmap->bits);
	spin_unlock(&bitmap->lock);
	kfree(bitmap);
}

int ubagg_bitmap_alloc_idx_from_offset_nolock(struct ubagg_bitmap *bitmap,
					      uint64_t offset)
{
	int idx;

	if (bitmap == NULL) {
		ubagg_log_err("bitmap NULL");
		return -1;
	}
	idx = (int)find_next_zero_bit(bitmap->bits, bitmap->size, offset);
	if (idx >= bitmap->size || idx < 0) {
		ubagg_log_err("bitmap allocation failed.\n");
		return -1;
	}

	set_bit(idx, bitmap->bits);
	ubagg_log_info("bitmap allocation success, idx = %d\n", idx);
	return idx;
}

int ubagg_bitmap_alloc_idx(struct ubagg_bitmap *bitmap)
{
	int idx;

	if (bitmap == NULL) {
		ubagg_log_err("bitmap NULL");
		return -1;
	}
	spin_lock(&bitmap->lock);
	idx = (int)find_first_zero_bit(bitmap->bits, bitmap->size);
	if (idx >= bitmap->size || idx < 0) {
		spin_unlock(&bitmap->lock);
		ubagg_log_err("bitmap allocation failed.\n");
		return -1;
	}
	set_bit(idx, bitmap->bits);
	spin_unlock(&bitmap->lock);
	ubagg_log_info("bitmap allocation success, idx = %d\n", idx);
	return idx;
}

int ubagg_bitmap_use_id(struct ubagg_bitmap *bitmap, uint32_t id)
{
	spin_lock(&bitmap->lock);
	if (test_bit(id, bitmap->bits) != 0) {
		spin_unlock(&bitmap->lock);
		ubagg_log_err("Bit %u is already taken.\n", id);
		return -1;
	}
	set_bit(id, bitmap->bits);
	spin_unlock(&bitmap->lock);
	return 0;
}

int ubagg_bitmap_free_idx(struct ubagg_bitmap *bitmap, int idx)
{
	spin_lock(&bitmap->lock);
	if (idx < 0) {
		spin_unlock(&bitmap->lock);
		ubagg_log_err("idx invalid, idx:%d.\n", idx);
		return -EINVAL;
	}
	if (test_bit(idx, bitmap->bits) == 0) {
		spin_unlock(&bitmap->lock);
		ubagg_log_err("idx not set: %d.\n", idx);
		return -EINVAL;
	}
	clear_bit(idx, bitmap->bits);
	spin_unlock(&bitmap->lock);
	return 0;
}

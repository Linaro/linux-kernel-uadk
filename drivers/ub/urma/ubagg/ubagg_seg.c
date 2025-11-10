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

#include "ubagg_seg.h"
#include "ubagg_bitmap.h"
#include "ubagg_log.h"

int ubagg_unregister_seg(struct ubcore_target_seg *seg)
{
	struct ubagg_seg_hash_node *seg_node = NULL;
	struct ubagg_device *ubagg_dev = NULL;

	if (!seg || !seg->ub_dev) {
		ubagg_log_err("Invalid param.\n");
		return -EINVAL;
	}

	ubagg_dev = to_ubagg_dev(seg->ub_dev);
	seg_node = (struct ubagg_seg_hash_node *)seg;

	ubagg_hash_table_remove(&ubagg_dev->ubagg_ht[UBAGG_HT_SEGMENT_HT],
				&seg_node->hnode);
	ubagg_bitmap_free_idx(ubagg_dev->segment_bitmap, seg_node->token_id);
	kfree(seg_node);
	return 0;
}

struct ubcore_target_seg *ubagg_register_seg(struct ubcore_device *dev,
					     struct ubcore_seg_cfg *cfg,
					     struct ubcore_udata *udata)
{
	struct ubagg_seg_hash_node *seg_node = NULL;
	struct ubagg_seg_hash_node *tmp_seg = NULL;
	struct ubagg_hash_table *ubagg_seg_ht = NULL;
	struct ubagg_device *ubagg_dev = NULL;
	int token_id = -1;
	int ret = 0;

	if (!dev || !cfg || !udata) {
		ubagg_log_err("Invalid param.\n");
		return ERR_PTR(-EINVAL);
	}

	ubagg_dev = to_ubagg_dev(dev);
	seg_node = kzalloc(sizeof(struct ubagg_seg_hash_node), GFP_KERNEL);
	if (!seg_node)
		return ERR_PTR(-ENOMEM);

	token_id = ubagg_bitmap_alloc_idx(ubagg_dev->segment_bitmap);
	if (token_id < 0) {
		ubagg_log_err("Fail to alloc token id.\n");
		goto FREE_SEG_NODE;
	}

	seg_node->ubagg_seg.seg.token_id = token_id;
	seg_node->token_id = token_id;
	seg_node->ubagg_seg.ub_dev = dev;
	ret = copy_from_user(&seg_node->ex_info,
			     (void __user *)udata->udrv_data->in_addr,
			     udata->udrv_data->in_len);
	if (ret != 0) {
		ubagg_log_err("Fail to copy data from user space, ret:%d.\n",
			      ret);
		goto FREE_TOKEN_ID;
	}

	ubagg_seg_ht = &ubagg_dev->ubagg_ht[UBAGG_HT_SEGMENT_HT];
	spin_lock(&ubagg_seg_ht->lock);
	tmp_seg = ubagg_hash_table_lookup_nolock(ubagg_seg_ht, token_id,
						 &token_id);
	if (tmp_seg != NULL) {
		// should remove it
		ubagg_hash_table_remove_nolock(ubagg_seg_ht, &tmp_seg->hnode);
		spin_unlock(&ubagg_seg_ht->lock);
		ubagg_log_err("Token id already exists.\n");
		kfree(tmp_seg);
		goto FREE_TOKEN_ID;
	}

	ubagg_hash_table_add_nolock(ubagg_seg_ht, &seg_node->hnode, token_id);
	spin_unlock(&ubagg_seg_ht->lock);

	return &seg_node->ubagg_seg;

FREE_TOKEN_ID:
	ubagg_bitmap_free_idx(ubagg_dev->segment_bitmap, token_id);
FREE_SEG_NODE:
	kfree(seg_node);
	return ERR_PTR(-EINVAL);
}

struct ubcore_target_seg *ubagg_import_seg(struct ubcore_device *dev,
					   struct ubcore_target_seg_cfg *cfg,
					   struct ubcore_udata *udata)
{
	struct ubagg_device *ubagg_dev = to_ubagg_dev(dev);
	struct ubcore_target_seg *tseg;

	if (ubagg_dev == NULL || cfg == NULL || udata == NULL ||
	    udata->uctx == NULL) {
		ubagg_log_err("Invalid param");
		return NULL;
	}

	tseg = kzalloc(sizeof(struct ubcore_target_seg), GFP_KERNEL);
	if (tseg == NULL)
		return NULL;

	return tseg;
}

int ubagg_unimport_seg(struct ubcore_target_seg *tseg)
{
	if (tseg == NULL || tseg->ub_dev == NULL || tseg->uctx == NULL) {
		ubagg_log_err("Invalid param");
		return -EINVAL;
	}

	kfree(tseg);
	return 0;
}

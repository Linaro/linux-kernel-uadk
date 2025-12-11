/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved.
 * Description: segment tree header file
 */

#ifndef __SEG_TREE_H__
#define __SEG_TREE_H__

enum range_compare_ret {
	RANGE_MATCH,
	RANGE_OVERLAP,
	RANGE_NOT_EXIST,
};

typedef int (*range_compare)(u64 left, u64 size, void *data);
typedef void (*clear_seg_node)(void *);

struct maple_tree *ummu_create_seg_tree(void);
int ummu_insert_seg(void *data, struct maple_tree *seg_tree, u64 left, u64 size);
void *ummu_find_seg(u64 left, u64 size, struct maple_tree *seg_tree, range_compare func, int *comp);
int ummu_delete_seg(u64 left, u64 size, struct maple_tree *seg_tree, range_compare func,
		    clear_seg_node cleaner);
void ummu_destroy_seg_tree(struct maple_tree *seg_tree, clear_seg_node cleaner);

#endif /* __SEG_TREE_H__ */

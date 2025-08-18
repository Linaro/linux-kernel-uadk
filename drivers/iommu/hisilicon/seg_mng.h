/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved.
 * Description: UMMU segment tree manager
 */

#ifndef __SEG_MNG_H__
#define __SEG_MNG_H__

#include <linux/maple_tree.h>

#include "perm_table.h"

enum ummu_grant_op_type ummu_grant_check(struct ummu_mapt_info *mapt_info,
	struct ummu_data_info *grant_param);
enum ummu_grant_op_type ummu_ungrant_check(struct ummu_mapt_info *mapt_info,
	struct ummu_data_info *grant_param);
struct maple_tree *ummu_create_seg_mng(void);
void ummu_destroy_seg_mng(struct maple_tree *seg_table);
int ummu_insert_new_addr(struct maple_tree *seg_table, struct ummu_data_info *grant_param);
int ummu_delete_addr(struct maple_tree *seg_table, struct ummu_data_info *grant_param);
int ummu_token_update(struct maple_tree *seg_table, struct ummu_data_info *grant_param,
		      enum ummu_grant_op_type op);

#endif  /* __SEG_MNG_H__ */

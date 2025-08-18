/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved.
 * Description: UMMU permission table
 */

#ifndef __UMMU_PERM_TABLE_H__
#define __UMMU_PERM_TABLE_H__

#include <linux/ummu_core.h>
#include <linux/maple_tree.h>

#define UMMU_MAX_TOKEN_NUM 2

struct ummu_mapt_entry_node {
	u32 valid : 1;
	u32 reserved_0 : 2;
	u32 e_bit : 1;
	u32 permission : 6;
	u32 reserved_1 : 22;
	u32 reserved_2;

	u32 base_low;
	u32 base_high : 16;
	u32 reserved_3 : 12;
	u32 reserved_4 : 3;
	u32 token_check : 1;

	u32 limit_low;
	u32 limit_high : 16;
	u32 reserved_5 : 12;
	u32 reserved_6 : 2;
	u32 nonce : 2;

	u32 token_val_0;
	u32 token_val_1;
};

#define ADDR_FULL(low, high) (((u64)(high) << 32) | (u64)(low))

struct ummu_mapt_table_ctx {
	struct maple_tree *granted_addr_mng;
};

struct ummu_mapt_info {
	enum ummu_mapt_mode mode;
	union {
		struct ummu_mapt_entry_node *entry_block;
		struct ummu_mapt_table_ctx *table_ctx;
	} block_base;
};

struct ummu_seg_info {
	u64 start_addr;
	u64 grant_size;
	enum ummu_mapt_perm permission;
	enum ummu_ebit_state e_bit;
	u8 token_check; /* true: check token  false: not check token */
	u8 token_count;
	u32 token_val[UMMU_MAX_TOKEN_NUM];
};

enum ummu_grant_op_type {
	UMMU_GRANT = 0,
	UMMU_ADD_TOKEN = 1,
	UMMU_REMOVE_TOKEN = 2,
	UMMU_UNGRANT = 3,
	UMMU_OP_END
};

struct ummu_data_info {
	size_t data_size;
	enum ummu_mapt_perm perm;
	u32 tokenval;
	u64 data_base;
	u64 data_limit;
	u32 token_check;
	int bytoken;
	enum ummu_ebit_state e_bit;
};

#endif /* __UMMU_PERM_TABLE_H__ */

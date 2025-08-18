/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved.
 * Description: UMMU Configuration table header file
 */

#ifndef __UMMU_CFG_TABLE_H__
#define __UMMU_CFG_TABLE_H__

#include <linux/types.h>
#include <linux/kref.h>
#include "ummu.h"

/* TECT's max space size: 6(4K)/8(16K)/10(64K) */
#define TECT_SPLIT 8
#define TECT_L1_ENTRY_BYTES 8

/* TECT Entry info */
#define TECT_ENTRY_SIZE_BYTES 64
#define TECT_ENTRY_SIZE_DWORDS 8

#define TECT_ENT0_V (1UL << 0)
#define TECT_ENT0_ST_MODE GENMASK(3, 1)
#define TECT_ENT0_ST_MODE_ABORT 0
#define TECT_ENT0_MAPT_EN (1UL << 19)

struct ummu_tecte_data {
	__le64 data[TECT_ENTRY_SIZE_DWORDS];
};

/* target context table */
#define TCT_L1_ENTRY_SIZE_BYTES 8

#define TCT_ENTRY_SIZE_BYTES 64
#define TCT_FMT_LINEAR 0
#define TCT_FMT_LVL2_64K 2

/*
 * Linear: when less than 1024 tids are supported
 * 2lvl: at most 1024 L1 entries,
 *       1024 lazy entries per table.
 */
#define TCT_SPLIT_64K 10
#define TCT_L2_ENTRIES (1UL << TCT_SPLIT_64K)

/*
 * When only supports linear target context tables, pick a
 * reasonable size limit (64kB).
 */
#define TCT_LINEAR_ENTS_MAX ilog2(SZ_64K / TCT_ENTRY_SIZE_BYTES)

/* USER LOGIC DEFINE */
#define UMMU_KV_TABLE_BASE_OFFSET 0x3800
#define KV_TABLE_BASE_ADDR_MASK GENMASK_ULL(51, 5)

#define UMMU_KV_TABLE_BASE_CFG_OFFSET 0x3808
#define KV_TABLE_DEPTH_MASK GENMASK(31, 16)
#define KV_TABLE_DEPTH 0x100
#define KV_TABLE_BANK_NUM_MASK GENMASK(15, 8)
#define KV_TABLE_BANK_NUM 8

#define UMMU_KV_TABLE_HASH_CFG0_OFFSET 0x380C
#define KV_TABLE_HASH_WIDTH_MASK GENMASK(7, 4)
#define KV_TABLE_HASH_WIDTH_8BIT 1
#define KV_TABLE_HASH_SEL_MASK GENMASK(3, 0)
#define KV_TABLE_HASH_CRC32 1

#define UMMU_KV_TABLE_HASH_CFG1_OFFSET 0x3810
#define CRC32_INIT_VALUE 0xFFFFFFFF

#define UMMU_CAM_TABLE_BASE_OFFSET 0x3820
#define CAM_TABLE_BASE_ADDR_MASK GENMASK_ULL(51, 5)

#define UMMU_CAM_TABLE_BASE_CFG_OFFSET 0x3828
#define CAM_TABLE_DEPTH_MASK GENMASK(31, 16)
#define CAM_TABLE_DEPTH 0x100

#define HASH_ENTRY_SIZE_BYTES 32

/* target OS config table */
struct ummu_tect_l1_desc {
	u32		l2_tecte_num;
	__le64		*l2ptr;
	phys_addr_t	l2ptr_pa;
};

struct ummu_tect_cfg {
	__le64				*tbl_vaddr;
	phys_addr_t			phys;
	size_t				tbl_size;
	unsigned int			num_ents;
	struct ummu_tect_l1_desc	*l1_tect_desc;
	u8				tect_fmt;
	u64				tect_reg_addr;
	u32				tect_reg_cfg;

	struct kref			ref;
};

const struct ummu_capability *ummu_get_cap(void);
int ummu_check_cap(struct ummu_device *ummu);
int ummu_prepare_tect_tct(struct ummu_device *ummu);
void ummu_device_set_tect(struct ummu_device *ummu);
int ummu_device_init_hash_table(struct ummu_device *ummu);
void ummu_device_config_hash_table(struct ummu_device *ummu);
void ummu_put_tct_table(struct ummu_tct_desc_cfg *cfg);
void ummu_put_tect_table(struct ummu_tect_cfg *tect);
int ummu_init_global_meta(void);
void ummu_free_global_meta(void);
#endif /* __UMMU_CFG_TABLE_H__ */

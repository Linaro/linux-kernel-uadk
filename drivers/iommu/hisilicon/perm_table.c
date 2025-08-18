// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved.
 * Description: UMMU permission table
 */

#define pr_fmt(fmt) "UMMU: " fmt

#include <linux/cleanup.h>

#include "ummu.h"
#include "flush.h"
#include "seg_mng.h"
#include "cfg_table.h"
#include "perm_table.h"

/* allocate based on the 4 KB memory size. */
#define UMMU_BLKTBL_MAX_ENTRIES (1UL << 9)

#define UMMU_BLKTBL_ENTRY_SIZE 8
#define UMMU_BLKTBL_ENTRY_DWORD 1

#define BLK_TABLE_ENT_V (1UL << 0)
#define BLK_PHY_OFFSET 5
#define BLK_ADDR_MASK GENMASK_ULL(47, 5)
#define BLK_SIZE_ORDER_MASK GENMASK_ULL(63, 60)

#define MAPT_VPAGE_SHIFT 12

/* the size of mapt is calculated at a granularity of 4KB */
#define MAPT_BLK_SZ_BASE_SHIFT 12

#define PAGE_ORDER_TO_MAPT_ORDER(page_order) \
	((int)(page_order) + PAGE_SHIFT - MAPT_BLK_SZ_BASE_SHIFT)

#define MAPT_ORDER_TO_PAGE_ORDER(mapt_order) \
	((int)(mapt_order) + MAPT_BLK_SZ_BASE_SHIFT - PAGE_SHIFT)

#define MAPT_MAX_LVL_INDEX 3
#define MAPT_MAX_ENTRY_INDEX 512
#define MAPT_MAX_LVL_ID_BIT_SIZE (UMMU_BLKTBL_MAX_ENTRIES << 2)

#define LVL_OFFSET_LOW(offset) FIELD_GET(GENMASK(19, 0), (offset))
#define LVL_OFFSET_HIGH(offset) FIELD_GET(GENMASK(29, 20), (offset))

static int ummu_alloc_mapt_block_tbl(struct io_pt_blk_table *blk_table)
{
	size_t size = UMMU_BLKTBL_MAX_ENTRIES * UMMU_BLKTBL_ENTRY_SIZE;
	void *addr;

	size = PAGE_ALIGN(size);
	addr = (__le64 *)__get_free_pages(GFP_KERNEL | __GFP_ZERO, get_order(size));
	if (!addr) {
		pr_err("allocate mapt block table(%lu bytes) failed\n", size);
		return -ENOMEM;
	}
	blk_table->blk_tbl_phys = virt_to_phys(addr);
	blk_table->blk_tbl_size_order = get_order(size);

	return 0;
}

static void ummu_release_mapt_block_tbl(struct io_pt_blk_table *blk_table)
{
	unsigned long addr;
	u32 page_num_order;

	if (!blk_table->blk_tbl_phys)
		return;

	page_num_order = blk_table->blk_tbl_size_order;
	addr = (unsigned long)phys_to_virt(blk_table->blk_tbl_phys);

	free_pages(addr, page_num_order);
	blk_table->blk_tbl_phys = 0;
}

static void ummu_write_block_desc(__le64 *dst, struct block_args *blk_para)
{
	u64 val = BLK_TABLE_ENT_V;
	u32 page_order;

	page_order = PAGE_ORDER_TO_MAPT_ORDER(blk_para->block_size_order);
	val |= (BLK_ADDR_MASK & blk_para->out_addr);
	val |= FIELD_PREP(BLK_SIZE_ORDER_MASK, page_order);
	WRITE_ONCE(*dst, cpu_to_le64(val));
}

static int ummu_get_mapt_mem(struct ummu_domain *ummu_domain,
			     struct block_args *blk_para)
{
	struct ummu_tct_desc *tct_desc = &ummu_domain->cfgs.s1_cfg.tct;
	u8 order;

	order = (u8)PAGE_ORDER_TO_MAPT_ORDER(blk_para->block_size_order);
	if (tct_desc->mapt_blk_phys && order == tct_desc->blk_size_order) {
		blk_para->out_addr = tct_desc->mapt_blk_phys;
		return 0;
	}

	return -ENOMEM;
}

static int ummu_alloc_mapt_mem_for_entry(struct ummu_domain *ummu_domain,
					 struct block_args *blk_para)
{
	struct ummu_tct_desc *tct_desc = &ummu_domain->cfgs.s1_cfg.tct;
	void *alloc_ptr;

	if (tct_desc->mapt_en)
		return ummu_get_mapt_mem(ummu_domain, blk_para);

	/* allocate new mapt blk */
	alloc_ptr = (void *)__get_free_pages(GFP_KERNEL | __GFP_COMP | __GFP_ZERO,
					     blk_para->block_size_order);
	if (!alloc_ptr) {
		pr_err("allocate mapt block(%lu bytes) failed\n",
		       (1U << blk_para->block_size_order) * PAGE_SIZE);
		return -ENOMEM;
	}
	blk_para->out_addr = virt_to_phys(alloc_ptr);
	tct_desc->mapt_en = 1;
	tct_desc->token_en = 0;
	tct_desc->mapt_mode = MAPT_MODE_ENTRY;
	tct_desc->mapt_blk_phys = blk_para->out_addr;
	tct_desc->blk_size_order =
		PAGE_ORDER_TO_MAPT_ORDER(blk_para->block_size_order);

	return 0;
}

static int ummu_alloc_mapt_mem_for_table(struct ummu_domain *ummu_domain,
					 struct block_args *blk_para)
{
	struct ummu_device *ummu = core_to_ummu_device(ummu_domain->base_domain.core_dev);
	struct ummu_tct_desc *tct_desc = &ummu_domain->cfgs.s1_cfg.tct;
	size_t blk_size = (1U << blk_para->block_size_order) * PAGE_SIZE;
	struct io_pt_blk_table blk_table;
	__le64 *cfg_ptr;
	void *alloc_ptr;
	int ret;

	if (blk_size < SZ_16K || blk_size > SZ_2M) {
		pr_err("mapt block size(%lu bytes) out of range\n", blk_size);
		return -EINVAL;
	}

	if (tct_desc->mapt_en && blk_para->index == 0)
		return ummu_get_mapt_mem(ummu_domain, blk_para);

	if (blk_para->index == 0 && ummu_alloc_mapt_block_tbl(&blk_table))
		return -ENOMEM;

	/* allocate new mapt blk */
	alloc_ptr = (void *)__get_free_pages(GFP_KERNEL | __GFP_COMP | __GFP_ZERO,
					     blk_para->block_size_order);
	if (!alloc_ptr) {
		pr_err("allocate mapt block(%lu bytes) failed.\n",
		       (1U << blk_para->block_size_order) * PAGE_SIZE);
		ret = -ENOMEM;
		goto err_out;
	}

	if (ummu->cap.prod_ver == NO_PROD_ID) {
		ret = ummu_device_check_pa_continuity(ummu,
			virt_to_phys(alloc_ptr),
			PAGE_ORDER_TO_MAPT_ORDER(blk_para->block_size_order),
			blk_para->index);
		if (ret) {
			pr_err("mapt block is discontinuous ret = %d\n", ret);
			free_pages((unsigned long)alloc_ptr, blk_para->block_size_order);
			goto err_out;
		}
	}

	blk_para->out_addr = virt_to_phys(alloc_ptr);

	if (blk_para->index == 0) {
		tct_desc->mapt_mode = MAPT_MODE_TABLE;
		tct_desc->mapt_en = 1;
		tct_desc->token_en = 0;
		tct_desc->mapt_blk_phys = blk_para->out_addr;
		tct_desc->blk_size_order =
			PAGE_ORDER_TO_MAPT_ORDER(blk_para->block_size_order);
		tct_desc->mapt_blk_tbl_phys = blk_table.blk_tbl_phys;
		tct_desc->blk_tbl_size_order =
			PAGE_ORDER_TO_MAPT_ORDER(blk_table.blk_tbl_size_order);
	}

	cfg_ptr = (__le64 *)phys_to_virt(tct_desc->mapt_blk_tbl_phys) +
		  blk_para->index * UMMU_BLKTBL_ENTRY_DWORD;
	ummu_write_block_desc(cfg_ptr, blk_para);
	return 0;
err_out:
	if (blk_para->index == 0)
		ummu_release_mapt_block_tbl(&blk_table);

	return ret;
}

int ummu_alloc_mapt_blk_mem(struct ummu_domain *ummu_domain,
			    struct block_args *blk_para)
{
	int mode;

	guard(mutex)(&ummu_domain->init_mutex);

	mode = ummu_domain->cfgs.s1_cfg.io_pt_cfg.mode;
	if (mode == MAPT_MODE_TABLE)
		return ummu_alloc_mapt_mem_for_table(ummu_domain, blk_para);
	else if (mode == MAPT_MODE_ENTRY)
		return ummu_alloc_mapt_mem_for_entry(ummu_domain, blk_para);
	else
		return -EINVAL;
}

int ummu_init_sva_mapt_context(struct ummu_domain *ummu_domain,
			       enum ummu_mapt_mode mode)
{
	struct block_args blk_para;
	int ret;

	if (mode == MAPT_MODE_TABLE) {
		blk_para.index = 0;
		blk_para.block_size_order = get_order(ummu_get_mapt_base_blk_size());
	} else {
		blk_para.block_size_order = get_order(PAGE_SIZE);
	}

	ret = ummu_alloc_mapt_blk_mem(ummu_domain, &blk_para);
	if (ret)
		pr_err("tid %u withe mode %u init sva mapt ctx failed, ret = %d.\n",
			ummu_domain->base_domain.tid, mode, ret);
	return ret;
}

static void ummu_free_blk_tbl_ent(__le64 *dst)
{
	unsigned long blk_ptr;
	phys_addr_t blk_phys;
	u32 free_page_order;
	bool ent_live;
	u64 val;

	val = le64_to_cpu(dst[0]);
	ent_live = !!(val & BLK_TABLE_ENT_V);
	if (!ent_live)
		return;

	WRITE_ONCE(dst[0], 0);

	blk_phys = FIELD_GET(BLK_ADDR_MASK, val) << BLK_PHY_OFFSET;
	free_page_order = FIELD_GET(BLK_SIZE_ORDER_MASK, val);
	free_page_order = MAPT_ORDER_TO_PAGE_ORDER(free_page_order);
	blk_ptr = (unsigned long)phys_to_virt(blk_phys);
	free_pages(blk_ptr, free_page_order);
}

static void ummu_release_mapt_for_entry(struct ummu_domain *ummu_domain)
{
	struct ummu_tct_desc *tct_desc = &ummu_domain->cfgs.s1_cfg.tct;
	phys_addr_t phys_addr = tct_desc->mapt_blk_phys;
	unsigned long addr;
	u32 size_order;

	tct_desc->mapt_en = 0;
	tct_desc->mapt_blk_phys = 0;
	ummu_write_tct_desc(core_to_ummu_device(ummu_domain->base_domain.core_dev),
			    &ummu_domain->cfgs, true);

	size_order = MAPT_ORDER_TO_PAGE_ORDER(tct_desc->blk_size_order);
	addr = (unsigned long)phys_to_virt(phys_addr);
	free_pages(addr, size_order);
}

static void ummu_release_mapt_for_table(struct ummu_domain *ummu_domain,
					u32 blk_index)
{
	struct ummu_tct_desc *tct_desc = &ummu_domain->cfgs.s1_cfg.tct;
	struct io_pt_blk_table blk_table;
	__le64 *tbl_ptr, *release_ptr;
	phys_addr_t phys_addr;

	phys_addr = tct_desc->mapt_blk_tbl_phys;
	tbl_ptr = phys_to_virt(phys_addr);
	if (!tbl_ptr)
		return;
	release_ptr = tbl_ptr + blk_index * UMMU_BLKTBL_ENTRY_DWORD;
	ummu_free_blk_tbl_ent(release_ptr);

	if (blk_index == 0) {
		blk_table.blk_tbl_phys = tct_desc->mapt_blk_tbl_phys;
		blk_table.blk_tbl_size_order =
			MAPT_ORDER_TO_PAGE_ORDER(tct_desc->blk_tbl_size_order);

		tct_desc->mapt_en = 0;
		tct_desc->mapt_blk_phys = 0;
		tct_desc->mapt_blk_tbl_phys = 0;
		ummu_write_tct_desc(core_to_ummu_device(ummu_domain->base_domain.core_dev),
				    &ummu_domain->cfgs, true);

		ummu_release_mapt_block_tbl(&blk_table);
	}
}

static void ummu_release_mapt_info_by_index(struct ummu_domain *ummu_domain,
					    u32 blk_index)
{
	enum ummu_mapt_mode mode;

	mode = ummu_domain->cfgs.s1_cfg.io_pt_cfg.mode;
	if (mode == MAPT_MODE_TABLE)
		ummu_release_mapt_for_table(ummu_domain, blk_index);
	else if (mode == MAPT_MODE_ENTRY)
		ummu_release_mapt_for_entry(ummu_domain);
	else
		pr_err("tid %u Get invalid mapt mode %u when release mapt\n",
		       ummu_domain->base_domain.tid, mode);
}

void ummu_release_domain_mapt_mem(struct ummu_domain *ummu_domain)
{
	struct ummu_tct_desc *tct_desc = &ummu_domain->cfgs.s1_cfg.tct;
	struct io_pt_blk_table blk_tbl;
	__le64 *blk_tbl_ptr, *ent_ptr;
	unsigned long addr;
	u32 size_order;
	int i, mode;

	guard(mutex)(&ummu_domain->init_mutex);

	if (!tct_desc->mapt_en || !tct_desc->mapt_blk_phys)
		return;

	/* copy tct config */
	mode = ummu_domain->cfgs.s1_cfg.io_pt_cfg.mode;
	if (mode == MAPT_MODE_TABLE) {
		blk_tbl_ptr = phys_to_virt(tct_desc->mapt_blk_tbl_phys);
		blk_tbl.blk_tbl_phys = tct_desc->mapt_blk_tbl_phys;
		blk_tbl.blk_tbl_size_order =
			MAPT_ORDER_TO_PAGE_ORDER(tct_desc->blk_tbl_size_order);
	}
	size_order = MAPT_ORDER_TO_PAGE_ORDER(tct_desc->blk_size_order);
	addr = (unsigned long)phys_to_virt(tct_desc->mapt_blk_phys);

	/* update tct to issue hardware stop */
	tct_desc->mapt_en = 0;
	tct_desc->mapt_blk_phys = 0;
	tct_desc->mapt_blk_tbl_phys = 0;
	ummu_write_tct_desc(core_to_ummu_device(ummu_domain->base_domain.core_dev),
			    &ummu_domain->cfgs, true);

	/* release mapt memory */
	if (mode == MAPT_MODE_TABLE) {
		for (i = 1; i < UMMU_BLKTBL_MAX_ENTRIES; i++) {
			ent_ptr = blk_tbl_ptr + i * UMMU_BLKTBL_ENTRY_DWORD;
			ummu_free_blk_tbl_ent(ent_ptr);
		}
		ummu_release_mapt_block_tbl(&blk_tbl);
	}
	free_pages(addr, size_order);
}

void ummu_release_mapt_blk_mem(struct ummu_domain *ummu_domain,
			       struct block_args *blk_para)
{
	struct ummu_tct_desc *tct_desc = &ummu_domain->cfgs.s1_cfg.tct;

	guard(mutex)(&ummu_domain->init_mutex);

	if (!tct_desc->mapt_en)
		return;

	ummu_release_mapt_info_by_index(ummu_domain, blk_para->index);
}

static struct ummu_mapt_block *ummu_alloc_new_mapt_block(struct ummu_mapt_info *mapt_info,
							 size_t blk_size,
							 u32 blk_idx,
							 struct ummu_domain *domain)
{
	struct ummu_mapt_block *mapt_blk;
	struct block_args blk_para;
	int ret;

	if ((blk_idx != 0 && !mapt_info->block_base.table_ctx->expan) ||
	    blk_idx >= UMMU_BLKTBL_MAX_ENTRIES)
		return ERR_PTR(-ERANGE);

	mapt_blk = kcalloc(1, sizeof(*mapt_blk), GFP_KERNEL);
	if (!mapt_blk)
		return ERR_PTR(-ENOMEM);

	blk_para.index = blk_idx;
	blk_para.block_size_order = get_order(blk_size);
	ret = ummu_alloc_mapt_blk_mem(domain, &blk_para);
	if (ret) {
		pr_err("alloc mapt mgnt info failed, ret = %d\n", ret);
		goto err_free_blk;
	}

	mapt_blk->blk_size = blk_size;
	mapt_blk->block_id = blk_idx;
	mapt_blk->block_addr = phys_to_virt(blk_para.out_addr);
	if (mapt_info->block_base.table_ctx->block_cnt == 0)
		mapt_info->block_base.table_ctx->mapt_block_base = mapt_blk;

	ret = xa_err(xa_store(&mapt_info->block_base.table_ctx->xa, blk_idx, mapt_blk, GFP_KERNEL));
	if (ret) {
		pr_err("xa_store blk mem failed, ret = %d\n", ret);
		ret = -EFAULT;
		goto err_release_blk_mem;
	}
	mapt_info->block_base.table_ctx->block_cnt++;

	return mapt_blk;

err_release_blk_mem:
	ummu_release_mapt_blk_mem(domain, &blk_para);
err_free_blk:
	kfree(mapt_blk);
	mapt_blk = NULL;
	return ERR_PTR(ret);
}

struct ummu_mapt_table_node *ummu_alloc_level_block(struct ummu_mapt_info *mapt_info,
						    struct ummu_mapt_table_node *pre_node,
						    struct ummu_mapt_block *pre_node_mapt_blk)
{
	struct ummu_mapt_table_node *node;
	struct ummu_mapt_block *mapt_blk;
	struct ummu_domain *u_domain;
	u32 blk_idx, lvl_id;
	u64 next_lvl_offset;
	size_t blk_size;

	lvl_id = find_first_zero_bit(mapt_info->block_base.table_ctx->level_block_bitmap,
				     MAPT_MAX_LVL_ID_BIT_SIZE);
	if (lvl_id >= MAPT_MAX_LVL_ID_BIT_SIZE) {
		pr_err("invalid level id = %u\n.", lvl_id);
		return ERR_PTR(-ERANGE);
	}

	pre_node->next_block = 0;
	blk_idx = lvl_id / MAPT_PER_LVL_BLOCK_CNT;
	blk_size = mapt_info->block_base.table_ctx->blk_exp_size;

	mapt_blk = xa_load(&mapt_info->block_base.table_ctx->xa, blk_idx);
	if (mapt_blk == NULL) {
		u_domain = to_ummu_domain(mapt_info->domain);
		mapt_blk = ummu_alloc_new_mapt_block(mapt_info, blk_size, blk_idx, u_domain);
		if (IS_ERR(mapt_blk)) {
			pr_err("alloc mapt block failed, tid = %u\n", u_domain->base_domain.tid);
			return ERR_PTR(PTR_ERR(mapt_blk));
		}
		pre_node->next_block = 1;
	} else {
		if (pre_node_mapt_blk != mapt_blk)
			pre_node->next_block = 1;
	}

	pre_node->next_lv_index = blk_idx;
	next_lvl_offset = MAPT_MAX_ENTRY_INDEX * (lvl_id % MAPT_PER_LVL_BLOCK_CNT);
	pre_node->next_lv_offset_low = LVL_OFFSET_LOW(next_lvl_offset);
	pre_node->next_lv_offset_high = LVL_OFFSET_HIGH(next_lvl_offset);

	set_bit(lvl_id, mapt_info->block_base.table_ctx->level_block_bitmap);
	node = (struct ummu_mapt_table_node *)mapt_blk->block_addr + next_lvl_offset;
	mapt_blk->level_cnt++;

	return node;
}

static int ummu_init_ksva_mapt_for_table(struct ummu_mapt_info *pt_cfg)
{
	struct ummu_mapt_table_ctx *table_ctx;
	struct ummu_mapt_table_node pre_node;
	struct ummu_mapt_table_node *root;
	int ret;

	table_ctx = kzalloc(sizeof(*table_ctx), GFP_KERNEL);
	if (!table_ctx)
		return -ENOMEM;

	xa_init(&table_ctx->xa);

	table_ctx->granted_addr_mng = ummu_create_seg_mng();
	if (!table_ctx->granted_addr_mng) {
		pr_err("init rbtree failed.\n");
		ret = -ENOMEM;
		goto err_create_rbtree;
	}

	table_ctx->expan = ummu_get_mapt_blk_exp();
	table_ctx->blk_exp_size = ummu_get_mapt_base_blk_size();
	if (!IS_ALIGNED((u64)table_ctx->blk_exp_size,
	    BIT_ULL(MAPT_VPAGE_SHIFT))) {
		pr_err("invalid blk exp size.\n");
		ret = -EINVAL;
		goto err_lvl_block_bitmap;
	}

	table_ctx->level_block_bitmap = bitmap_zalloc(MAPT_MAX_LVL_ID_BIT_SIZE, GFP_KERNEL);
	if (!table_ctx->level_block_bitmap) {
		ret = -ENOMEM;
		goto err_lvl_block_bitmap;
	}

	pt_cfg->block_base.table_ctx = table_ctx;
	root = ummu_alloc_level_block(pt_cfg, &pre_node, NULL);
	if (IS_ERR(root)) {
		ret = PTR_ERR(root);
		goto err_lvl_blk_alloc;
	}
	if (!root) {
		ret = -ENOMEM;
		goto err_lvl_blk_alloc;
	}

	return 0;

err_lvl_blk_alloc:
	bitmap_free(table_ctx->level_block_bitmap);
err_lvl_block_bitmap:
	ummu_destroy_seg_mng(table_ctx->granted_addr_mng);
err_create_rbtree:
	xa_destroy(&table_ctx->xa);
	kfree(table_ctx);
	return ret;
}

static int ummu_init_ksva_mapt_for_entry(struct ummu_domain *domain, size_t size)
{
	struct ummu_mapt_entry_node *node;
	struct block_args blk_para;
	int ret;

	blk_para.block_size_order = get_order(size);
	ret = ummu_alloc_mapt_blk_mem(domain, &blk_para);
	if (ret)
		return ret;

	node = phys_to_virt(blk_para.out_addr);
	node->valid = 0;
	node->nonce = 0;
	domain->cfgs.s1_cfg.io_pt_cfg.block_base.entry_block = node;
	return 0;
}

int ummu_init_ksva_mapt(struct ummu_domain *domain, enum ummu_mapt_mode mode)
{
	struct ummu_domain_cfgs *cfg = &domain->cfgs;
	struct ummu_mapt_info *pt_cfg = &cfg->s1_cfg.io_pt_cfg;
	int ret;

	pt_cfg->mode = mode;

	if (mode == MAPT_MODE_TABLE)
		ret = ummu_init_ksva_mapt_for_table(pt_cfg);
	else
		ret = ummu_init_ksva_mapt_for_entry(domain, PAGE_SIZE);

	if (ret) {
		pr_err("init ksva mapt info failed, ret = %d.\n", ret);
		return ret;
	}

	pt_cfg->valid = 1;
	return 0;
}

void ummu_release_ksva_mapt(struct ummu_domain *domain)
{
	struct ummu_mapt_info *pt_cfg = &domain->cfgs.s1_cfg.io_pt_cfg;
	enum ummu_mapt_mode mode = pt_cfg->mode;
	struct ummu_mapt_block *mapt_blk;
	unsigned long index;

	if (!pt_cfg->valid)
		return;

	if (mode == MAPT_MODE_TABLE) {
		xa_for_each(&pt_cfg->block_base.table_ctx->xa, index, mapt_blk)
			kfree(mapt_blk);

		xa_destroy(&pt_cfg->block_base.table_ctx->xa);
		bitmap_free(pt_cfg->block_base.table_ctx->level_block_bitmap);
		ummu_destroy_seg_mng(pt_cfg->block_base.table_ctx->granted_addr_mng);
		kfree(pt_cfg->block_base.table_ctx);
	}

	ummu_release_domain_mapt_mem(domain);
}

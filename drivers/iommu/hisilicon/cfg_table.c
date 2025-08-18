// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved.
 * Description: UMMU Configuration Table Management.
 */
#define pr_fmt(fmt) "UMMU: " fmt

#include <linux/xarray.h>
#include <linux/ummu_core.h>
#include <linux/cleanup.h>

#include "ummu.h"
#include "queue.h"
#include "flush.h"
#include "regs.h"
#include "cfg_table.h"

static DEFINE_MUTEX(ummu_dev_tct_lock);

struct os_meta {
	u32 tecte_tag;
	guid_t guid;
	struct ummu_tct_desc_cfg tct_tbl;
	struct list_head eids;
	struct kref ref;
	bool valid;
};

struct eid_info {
	eid_t eid;
	int eid_type;
	u32 kv_index;
	struct list_head node;
	int ref_cnt;
};

static struct {
	struct xarray per_os_meta;
	struct ummu_tect_cfg ummu_tect;
	bool tect_valid;
	struct ummu_capability cap;
	bool cap_valid;
} ummu_global_info;

/* protect ummu_global_info */
static DEFINE_SPINLOCK(ummu_global);

static int ummu_init_tect(const struct ummu_capability *cap,
			  struct ummu_tect_cfg *tect);

static int ummu_alloc_tct(const struct ummu_capability *cap,
			  struct ummu_tct_desc_cfg *tct_cfg);
static void ummu_free_tct(struct ummu_tct_desc_cfg *tct_cfg);

static void ummu_free_tct_table(struct kref *kref)
{
	struct ummu_tct_desc_cfg *cfg =
		container_of(kref, struct ummu_tct_desc_cfg, ref);

	if (cfg->tct_ptr)
		ummu_free_tct(cfg);
}

void ummu_put_tct_table(struct ummu_tct_desc_cfg *cfg)
{
	kref_put(&cfg->ref, ummu_free_tct_table);
}

static void free_os_meta(struct kref *ref)
{
	struct os_meta *meta = container_of(ref, struct os_meta, ref);

	ummu_put_tct_table(&meta->tct_tbl);
	xa_erase(&ummu_global_info.per_os_meta, meta->tecte_tag);
	kfree(meta);
}

const struct ummu_capability *ummu_get_cap(void)
{
	guard(spinlock)(&ummu_global);
	if (!ummu_global_info.cap_valid)
		return NULL;

	return &ummu_global_info.cap;
}

static struct ummu_tct_desc_cfg *ummu_get_local_tct_table(void)
{
	struct ummu_tct_desc_cfg *cfg;
	struct os_meta *meta;
	int ret;

	guard(spinlock)(&ummu_global);
	if (!ummu_global_info.tect_valid)
		return NULL;

	meta = xa_load(&ummu_global_info.per_os_meta, 0);
	if (!meta)
		return NULL;

	cfg = &meta->tct_tbl;
	if (!cfg->tct_ptr) {
		if (!ummu_global_info.cap_valid)
			return NULL;

		ret = ummu_alloc_tct(&ummu_global_info.cap, cfg);
		if (ret)
			return NULL;
	}
	kref_get(&meta->tct_tbl.ref);

	return cfg;
}

static struct ummu_tect_cfg *ummu_get_tect_table(void)
{
	struct ummu_tect_cfg *tect;
	int ret;

	guard(spinlock)(&ummu_global);
	tect = &ummu_global_info.ummu_tect;
	if (!tect->tbl_vaddr) {
		if (!ummu_global_info.cap_valid)
			return NULL;
		ret = ummu_init_tect(&ummu_global_info.cap, tect);
		if (ret)
			return NULL;
	}

	kref_get(&tect->ref);

	return tect;
}

static void ummu_free_tect(struct ummu_tect_cfg *tect)
{
	u32 l1_size, l2_size;
	unsigned int i;

	if (tect->l1_tect_desc) {
		l2_size = (1UL << TECT_SPLIT) * TECT_ENTRY_SIZE_BYTES;
		for (i = 0; i < tect->num_ents; i++) {
			if (!tect->l1_tect_desc[i].l2ptr)
				continue;

			free_pages((unsigned long)tect->l1_tect_desc[i].l2ptr,
					   get_order(l2_size));
			tect->l1_tect_desc[i].l2ptr = NULL;
		}
		kfree(tect->l1_tect_desc);
		tect->l1_tect_desc = NULL;
		l1_size = tect->num_ents * TECT_L1_ENTRY_BYTES;
	} else {
		l1_size = tect->num_ents * TECT_ENTRY_SIZE_BYTES;
	}

	free_pages((unsigned long)tect->tbl_vaddr, get_order(l1_size));
	tect->tbl_vaddr = NULL;
	tect->phys = 0;
}

static void ummu_free_tect_table(struct kref *kref)
{
	struct ummu_tect_cfg *tect = container_of(kref, struct ummu_tect_cfg, ref);

	if (tect->tbl_vaddr)
		ummu_free_tect(tect);
}

void ummu_put_tect_table(struct ummu_tect_cfg *tect)
{
	kref_put(&tect->ref, ummu_free_tect_table);
}

static inline void ummu_build_tecte_abort(struct ummu_tecte_data *tecte)
{
	memset(tecte, 0, sizeof(*tecte));
	tecte->data[0] = cpu_to_le64(
		FIELD_PREP(TECT_ENT0_ST_MODE, TECT_ENT0_ST_MODE_ABORT));
}

static void ummu_tecte_pre_init(struct ummu_tecte_data *tect, u32 num_ents)
{
	u64 val;
	u32 i;

	for (i = 0; i < num_ents; ++i) {
		val = le64_to_cpu(tect->data[0]);
		if (val & TECT_ENT0_V)
			continue;

		ummu_build_tecte_abort(tect);

		tect->data[0] |= cpu_to_le64(TECT_ENT0_MAPT_EN);
		tect++;
	}
}

/* The TECT memory layout implementation is based on the ARM SMMU STE reference.*/
static int ummu_init_tect_linear(struct ummu_tect_cfg *tect)
{
	u32 size = PAGE_ALIGN(tect->num_ents * TECT_ENTRY_SIZE_BYTES);
	u32 reg;

	tect->tbl_vaddr = (__le64 *)__get_free_pages(GFP_ATOMIC | __GFP_ZERO,
						     get_order(size));
	if (!tect->tbl_vaddr)
		return -ENOMEM;

	tect->phys = virt_to_phys(tect->tbl_vaddr);
	tect->tbl_size = size;

	/* Configure tect_reg_cfg for linear table */
	reg = FIELD_PREP(TECT_BASE_CFG_FMT, tect->tect_fmt);
	reg |= FIELD_PREP(TECT_BASE_CFG_LOG2SIZE, ilog2(tect->num_ents));
	tect->tect_reg_cfg = reg;

	ummu_tecte_pre_init((struct ummu_tecte_data *)(tect->tbl_vaddr),
			    tect->num_ents);
	return 0;
}

static int ummu_init_tect_first_lvl(struct ummu_tect_cfg *tect)
{
	u32 l1_size = PAGE_ALIGN(tect->num_ents * TECT_L1_ENTRY_BYTES);
	u32 reg;

	tect->tbl_vaddr = (__le64 *)__get_free_pages(GFP_ATOMIC | __GFP_ZERO,
						     get_order(l1_size));
	if (!tect->tbl_vaddr)
		return -ENOMEM;

	tect->phys = virt_to_phys(tect->tbl_vaddr);
	tect->tbl_size = l1_size;

	/* Configure tect_reg_cfg for 2 levels tect */
	reg = FIELD_PREP(TECT_BASE_CFG_FMT, tect->tect_fmt);
	reg |= FIELD_PREP(TECT_BASE_CFG_SPLIT, TECT_SPLIT);
	reg |= FIELD_PREP(TECT_BASE_CFG_LOG2SIZE,
			  ilog2(tect->num_ents) + TECT_SPLIT);
	tect->tect_reg_cfg = reg;

	tect->l1_tect_desc = kcalloc(tect->num_ents,
		sizeof(struct ummu_tect_l1_desc), GFP_ATOMIC);
	if (!tect->l1_tect_desc) {
		free_pages((unsigned long)tect->tbl_vaddr, get_order(l1_size));
		tect->tbl_vaddr = NULL;
		return -ENOMEM;
	}

	return 0;
}

static int ummu_init_tect(const struct ummu_capability *cap,
			  struct ummu_tect_cfg *tect)
{
	u64 reg_val;
	u32 ent_bit;
	int ret;

	if (cap->features & UMMU_FEAT_2_LVL_TECT) {
		ent_bit = cap->deid_bits - TECT_SPLIT;
		tect->num_ents = 1UL << ent_bit;
		tect->tect_fmt = TECT_BASE_CFG_FMT_2LVL;
		ret = ummu_init_tect_first_lvl(tect);
	} else {
		tect->num_ents = 1UL << cap->deid_bits;
		tect->tect_fmt = TECT_BASE_CFG_FMT_LINEAR;
		ret = ummu_init_tect_linear(tect);
	}

	if (ret)
		return ret;

	/* configure tect base address */
	reg_val = tect->phys & TECT_BASE_ADDR_MASK;
	reg_val |= TECT_BASE_RA;
	tect->tect_reg_addr = reg_val;

	return 0;
}

void ummu_free_global_meta(void)
{
	struct os_meta *meta;
	unsigned long idx;

	guard(spinlock)(&ummu_global);
	xa_for_each(&ummu_global_info.per_os_meta, idx, meta)
		kref_put(&meta->ref, free_os_meta);

	xa_destroy(&ummu_global_info.per_os_meta);

	ummu_put_tect_table(&ummu_global_info.ummu_tect);
	ummu_global_info.tect_valid = false;
	ummu_global_info.cap_valid = false;
}

int ummu_init_global_meta(void)
{
	struct os_meta *meta;
	int ret;

	spin_lock_init(&ummu_global);
	if (ummu_global_info.tect_valid)
		return -EEXIST;

	meta = kzalloc(sizeof(*meta), GFP_KERNEL);
	if (!meta)
		return -ENOMEM;

	meta->tecte_tag = 0;
	memset(&meta->guid, 0, sizeof(meta->guid));
	INIT_LIST_HEAD(&meta->eids);
	/* 0 for local host tecte */
	xa_init_flags(&ummu_global_info.per_os_meta, XA_FLAGS_ALLOC1);
	ret = xa_err(xa_store(&ummu_global_info.per_os_meta,
			      0, meta, GFP_KERNEL));
	if (ret)
		goto free_meta;

	kref_init(&meta->tct_tbl.ref);
	kref_init(&ummu_global_info.ummu_tect.ref);
	kref_init(&meta->ref);
	ummu_global_info.tect_valid = true;
	return 0;

free_meta:
	xa_destroy(&ummu_global_info.per_os_meta);
	kfree(meta);
	return ret;
}

int ummu_prepare_tect_tct(struct ummu_device *ummu)
{
	struct ummu_tct_desc_cfg *local;
	struct ummu_tect_cfg *tect;
	int ret;

	tect = ummu_get_tect_table();
	if (!tect)
		return -EINVAL;

	local = ummu_get_local_tct_table();
	if (!local) {
		ret = -EINVAL;
		goto put_tect;
	}

	/* prepare tect */
	ummu->tect_cfg = tect;

	/* prepare tct */
	ummu->local_tct_cfg = local;
	return 0;

put_tect:
	ummu_put_tect_table(tect);
	return ret;
}

int ummu_check_cap(struct ummu_device *ummu)
{
	int ret;

	guard(spinlock)(&ummu_global);
	if (ummu_global_info.cap_valid) {
		ret = memcmp(&ummu->cap, &ummu_global_info.cap, sizeof(ummu->cap));
		WARN(ret, "expect all ummu instances has same capability!\n");
		return ret;
	}
	ummu_global_info.cap = ummu->cap;
	ummu_global_info.cap_valid = true;
	return 0;
}

static int ummu_alloc_tct_linear(struct ummu_tct_desc_cfg *tct_cfg)
{
	u32 tbl_size;

	tbl_size = tct_cfg->l1_tcte_num * TCT_ENTRY_SIZE_BYTES;
	tbl_size = PAGE_ALIGN(tbl_size);

	tct_cfg->tct_ptr = (__le64 *)__get_free_pages(GFP_ATOMIC | __GFP_ZERO,
						      get_order(tbl_size));
	if (!tct_cfg->tct_ptr)
		return -ENOMEM;
	tct_cfg->tct_phys_addr = virt_to_phys(tct_cfg->tct_ptr);

	return 0;
}

static int ummu_alloc_tct_first_lvl(struct ummu_tct_desc_cfg *tct_cfg)
{
	unsigned long l1_tcte_num = tct_cfg->l1_tcte_num;
	unsigned long l1_tbl_size;
	int ret;

	tct_cfg->l1_tct_desc = kcalloc(l1_tcte_num,
		sizeof(*tct_cfg->l1_tct_desc), GFP_ATOMIC);
	if (!tct_cfg->l1_tct_desc)
		return -ENOMEM;

	l1_tbl_size = l1_tcte_num * TCT_L1_ENTRY_SIZE_BYTES;
	l1_tbl_size = PAGE_ALIGN(l1_tbl_size);

	tct_cfg->tct_ptr = (__le64 *)__get_free_pages(GFP_ATOMIC | __GFP_ZERO,
						      get_order(l1_tbl_size));
	if (!tct_cfg->tct_ptr) {
		ret = -ENOMEM;
		goto err_free_l1_desc;
	}

	tct_cfg->tct_phys_addr = virt_to_phys(tct_cfg->tct_ptr);

	return 0;

err_free_l1_desc:
	kfree(tct_cfg->l1_tct_desc);
	tct_cfg->l1_tct_desc = NULL;
	return ret;
}

static int ummu_alloc_tct(const struct ummu_capability *cap,
			  struct ummu_tct_desc_cfg *tct_cfg)
{
	if (cap->features & UMMU_FEAT_2_LVL_TCT) {
		tct_cfg->tcte_max_bits = cap->tid_bits;
		tct_cfg->tct_fmt = TCT_FMT_LVL2_64K;
		tct_cfg->l1_tcte_num =
			DIV_ROUND_UP(1 << tct_cfg->tcte_max_bits, TCT_L2_ENTRIES);
		return ummu_alloc_tct_first_lvl(tct_cfg);
	}
	tct_cfg->tcte_max_bits = min(cap->tid_bits, TCT_LINEAR_ENTS_MAX);
	tct_cfg->tct_fmt = TCT_FMT_LINEAR;
	tct_cfg->l1_tcte_num = 1 << tct_cfg->tcte_max_bits;
	return ummu_alloc_tct_linear(tct_cfg);
}

static void ummu_free_tct(struct ummu_tct_desc_cfg *tct_cfg)
{
	size_t l1_size, l2_size;
	unsigned int i;

	if (tct_cfg->l1_tct_desc) {
		l2_size = TCT_L2_ENTRIES * TCT_ENTRY_SIZE_BYTES;
		for (i = 0; i < tct_cfg->l1_tcte_num; i++) {
			if (!tct_cfg->l1_tct_desc[i].l2ptr)
				continue;

			free_pages((unsigned long)tct_cfg->l1_tct_desc[i].l2ptr,
					   get_order(l2_size));
			tct_cfg->l1_tct_desc[i].l2ptr = NULL;
		}

		kfree(tct_cfg->l1_tct_desc);
		tct_cfg->l1_tct_desc = NULL;
		l1_size = tct_cfg->l1_tcte_num * TCT_L1_ENTRY_SIZE_BYTES;
	} else {
		l1_size = tct_cfg->l1_tcte_num * TCT_ENTRY_SIZE_BYTES;
	}

	free_pages((unsigned long)tct_cfg->tct_ptr, get_order(l1_size));
	tct_cfg->tct_ptr = NULL;
	tct_cfg->tct_phys_addr = 0;
}

void ummu_device_set_tect(struct ummu_device *ummu)
{
	writeq_relaxed(ummu->tect_cfg->tect_reg_addr,
		ummu->base + UMMU_TECT_BASE);
	writel_relaxed(ummu->tect_cfg->tect_reg_cfg,
		ummu->base + UMMU_TECT_BASE_CFG);
}

int ummu_device_init_hash_table(struct ummu_device *ummu)
{
	void *kv_addr, *cam_addr;
	size_t ents, kv_size, cam_size;
	phys_addr_t kv_phys, cam_phys;
	u32 val;

	ents = KV_TABLE_DEPTH * KV_TABLE_BANK_NUM;
	kv_size = PAGE_ALIGN(ents * HASH_ENTRY_SIZE_BYTES);
	cam_size = PAGE_ALIGN(CAM_TABLE_DEPTH * HASH_ENTRY_SIZE_BYTES);

	kv_addr = (void *)devm_get_free_pages(
		ummu->dev, GFP_KERNEL | __GFP_ZERO, get_order(kv_size + cam_size));
	if (!kv_addr) {
		dev_err(ummu->dev, "allocate kv table failed(%zu bytes).\n",
			kv_size + cam_size);
		return -ENOMEM;
	}
	kv_phys = virt_to_phys(kv_addr);
	cam_addr = kv_addr + kv_size;
	cam_phys = kv_phys + kv_size;

	ummu->hash_tbl_cfg.bank_depth = KV_TABLE_DEPTH;
	ummu->hash_tbl_cfg.bank_num = KV_TABLE_BANK_NUM;
	ummu->hash_tbl_cfg.hash_sel = KV_TABLE_HASH_CRC32;
	ummu->hash_tbl_cfg.hash_width = KV_TABLE_HASH_WIDTH_8BIT;
	ummu->hash_tbl_cfg.kv_tbl_vaddr = kv_addr;
	ummu->hash_tbl_cfg.kv_tbl_reg_addr = kv_phys & KV_TABLE_BASE_ADDR_MASK;
	val = FIELD_PREP(KV_TABLE_DEPTH_MASK, KV_TABLE_DEPTH) |
	      FIELD_PREP(KV_TABLE_BANK_NUM_MASK, KV_TABLE_BANK_NUM);
	ummu->hash_tbl_cfg.kv_tbl_reg_cfg = val;

	ummu->hash_tbl_cfg.cam_tbl_depth = CAM_TABLE_DEPTH;
	ummu->hash_tbl_cfg.cam_tbl_vaddr = cam_addr;
	ummu->hash_tbl_cfg.cam_tbl_reg_addr = cam_phys & CAM_TABLE_BASE_ADDR_MASK;

	val = FIELD_PREP(CAM_TABLE_DEPTH_MASK, CAM_TABLE_DEPTH);
	ummu->hash_tbl_cfg.cam_tbl_reg_cfg = val;
	return 0;
}

void ummu_device_config_hash_table(struct ummu_device *ummu)
{
	u32 reg;

	writeq_relaxed(ummu->hash_tbl_cfg.kv_tbl_reg_addr,
		       ummu->base + UMMU_KV_TABLE_BASE_OFFSET);

	writel_relaxed(ummu->hash_tbl_cfg.kv_tbl_reg_cfg,
		       ummu->base + UMMU_KV_TABLE_BASE_CFG_OFFSET);

	reg = FIELD_PREP(KV_TABLE_HASH_WIDTH_MASK, KV_TABLE_HASH_WIDTH_8BIT) |
	      FIELD_PREP(KV_TABLE_HASH_SEL_MASK, KV_TABLE_HASH_CRC32);
	writel_relaxed(reg,
		       ummu->base + UMMU_KV_TABLE_HASH_CFG0_OFFSET);

	writel_relaxed(CRC32_INIT_VALUE,
		       ummu->base + UMMU_KV_TABLE_HASH_CFG1_OFFSET);

	writeq_relaxed(ummu->hash_tbl_cfg.cam_tbl_reg_addr,
		       ummu->base + UMMU_CAM_TABLE_BASE_OFFSET);

	writel_relaxed(ummu->hash_tbl_cfg.cam_tbl_reg_cfg,
		       ummu->base + UMMU_CAM_TABLE_BASE_CFG_OFFSET);
}

// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved.
 * Description: UMMU matt allocator.
 */

#define pr_fmt(fmt) "UMMU: " fmt
#include <linux/dma-mapping.h>
#include <linux/io-pgtable.h>
#include <linux/errno.h>
#include <linux/kvm_host.h>

#include "flush.h"
#include "cfg_table.h"
#include "page_table.h"

struct ummu_pgtable_cfg {
	enum io_pgtable_fmt fmt;
	struct io_pgtable_cfg pgtbl_cfg;
	struct io_pgtable_ops *pgtbl_ops;
};

static const struct iommu_flush_ops ummu_flush_ops = {
	.tlb_flush_all = ummu_tlbi_context,
	.tlb_flush_walk = ummu_tlbi_walk,
	.tlb_add_page = ummu_tlbi_page,
};

struct ummu_domain identity_u_domain = {};

static int ummu_get_pgtable_cfg(struct ummu_domain *ummu_domain,
				struct ummu_pgtable_cfg *cfg)
{
	struct ummu_device *ummu = core_to_ummu_device(
					ummu_domain->base_domain.core_dev);

	if (ummu_domain->cfgs.stage == UMMU_DOMAIN_S1) {
		cfg->fmt = ARM_64_LPAE_S1;
		cfg->pgtbl_cfg.ias = (ummu->cap.features & UMMU_FEAT_VAX) ? 52 : 48;
		cfg->pgtbl_cfg.ias = min(cfg->pgtbl_cfg.ias, VA_BITS);
		cfg->pgtbl_cfg.oas = ummu->cap.ias;

		if (ummu->cap.features & UMMU_FEAT_HD)
			cfg->pgtbl_cfg.quirks |= IO_PGTABLE_QUIRK_ARM_HD;

		if (ummu->cap.features & UMMU_FEAT_BBML1)
			cfg->pgtbl_cfg.quirks |= IO_PGTABLE_QUIRK_ARM_BBML1;

		if (ummu->cap.features & UMMU_FEAT_BBML2)
			cfg->pgtbl_cfg.quirks |= IO_PGTABLE_QUIRK_ARM_BBML2;
	} else {
		cfg->fmt = ARM_64_LPAE_S2;
		cfg->pgtbl_cfg.ias = ummu->cap.ias;
		cfg->pgtbl_cfg.oas = ummu->cap.oas;
	}

	cfg->pgtbl_cfg.coherent_walk = ummu->cap.features & UMMU_FEAT_COHERENCY;
	cfg->pgtbl_cfg.pgsize_bitmap = ummu->cap.pgsize_bitmap;
	cfg->pgtbl_cfg.tlb = &ummu_flush_ops;
	cfg->pgtbl_cfg.iommu_dev = ummu->dev;

	cfg->pgtbl_ops =
		alloc_io_pgtable_ops(cfg->fmt, &cfg->pgtbl_cfg, ummu_domain);
	if (WARN_ON(!cfg->pgtbl_ops)) {
		pr_err("alloc page table failed, check page table config!\n");
		return -ENOMEM;
	}

	return 0;
}

static int ummu_domain_collect_pgtable_s1(struct ummu_domain *ummu_domain,
					  struct io_pgtable_cfg *pgtbl_cfg)
{
	typeof(&pgtbl_cfg->arm_lpae_s1_cfg.tcr) tcr =
		&pgtbl_cfg->arm_lpae_s1_cfg.tcr;
	struct ummu_s1_cfg *cfg = &ummu_domain->cfgs.s1_cfg;

	cfg->tct.ttbr = pgtbl_cfg->arm_lpae_s1_cfg.ttbr;
	cfg->tct.mair = pgtbl_cfg->arm_lpae_s1_cfg.mair;
	cfg->tct.tcr0 |= FIELD_PREP(TCT_ENT0_GPAS, tcr->ips) |
			 TCT_ENT0_FBR | TCT_ENT0_FBA | TCT_ENT0_AA64;

	cfg->tct.tcr1 |= FIELD_PREP(TCT_ENT1_MD0, tcr->irgn) |
			 FIELD_PREP(TCT_ENT1_MD1, tcr->orgn) |
			 FIELD_PREP(TCT_ENT1_SZ, tcr->tsz) |
			 FIELD_PREP(TCT_ENT1_TGS, tcr->tg) |
			 FIELD_PREP(TCT_ENT1_MSD, tcr->sh);

	return 0;
}

static int ummu_domain_collect_pgtable_s2(struct ummu_domain *ummu_domain,
					  struct io_pgtable_cfg *pgtbl_cfg)
{
	typeof(&pgtbl_cfg->arm_lpae_s2_cfg.vtcr) vtcr =
		&pgtbl_cfg->arm_lpae_s2_cfg.vtcr;
	struct ummu_s2_cfg *cfg = &ummu_domain->cfgs.s2_cfg;
	int vmid = 0;

	if (ummu_domain->kvm)
		vmid = kvm_pinned_vmid_get(ummu_domain->kvm);
	if (vmid < 0)
		return vmid;

	cfg->vmid = (u16)vmid;
	cfg->vttbr = pgtbl_cfg->arm_lpae_s2_cfg.vttbr;
	cfg->vtcr = FIELD_PREP(TECT_ENT2_NS_S2_TSZ, vtcr->tsz) |
		    FIELD_PREP(TECT_ENT2_NS_S2_SL, vtcr->sl) |
		    FIELD_PREP(TECT_ENT2_S2_MD0, vtcr->irgn) |
		    FIELD_PREP(TECT_ENT2_S2_MD1, vtcr->orgn) |
		    FIELD_PREP(TECT_ENT2_NS_S2_TG, vtcr->tg) |
		    FIELD_PREP(TECT_ENT2_S2_MSD, vtcr->sh) |
		    FIELD_PREP(TECT_ENT2_S2_PAS, vtcr->ps);

	return 0;
}

int ummu_domain_collect_pgtable(struct ummu_domain *ummu_domain)
{
	struct ummu_pgtable_cfg cfg;
	int ret;

	memset(&cfg, 0, sizeof(cfg));
	ret = ummu_get_pgtable_cfg(ummu_domain, &cfg);
	if (ret) {
		pr_err("get ummu pagetable configuration failed!\n");
		return ret;
	}

	if (ummu_domain->cfgs.stage == UMMU_DOMAIN_S1)
		ret = ummu_domain_collect_pgtable_s1(ummu_domain,
						     &cfg.pgtbl_cfg);
	else
		ret = ummu_domain_collect_pgtable_s2(ummu_domain,
						     &cfg.pgtbl_cfg);

	if (ret) {
		free_io_pgtable_ops(cfg.pgtbl_ops);
		return ret;
	}

	ummu_domain->cfgs.pgtbl_ops = cfg.pgtbl_ops;
	ummu_domain->base_domain.domain.pgsize_bitmap =
				cfg.pgtbl_cfg.pgsize_bitmap;
	ummu_domain->base_domain.domain.geometry.aperture_start = 0;
	ummu_domain->base_domain.domain.geometry.aperture_end =
		(dma_addr_t)DMA_BIT_MASK(cfg.pgtbl_cfg.ias);
	ummu_domain->base_domain.domain.geometry.force_aperture = true;
	return 0;
}

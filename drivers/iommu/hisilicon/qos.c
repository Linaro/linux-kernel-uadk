// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved.
 * Description: ummu mpam internal interface.
 */

#define pr_fmt(fmt) "UMMU: " fmt

#include <linux/iopoll.h>
#include <linux/errno.h>
#include <linux/bitfield.h>
#include <linux/kernel.h>

#include "regs.h"
#include "flush.h"
#include "cfg_table.h"
#include "logic_ummu/logic_ummu.h"
#include "qos.h"

static int ummu_check_mpam_cap_info(struct ummu_device *ummu,
				    u32 partid, u32 pmg)
{
	if (!(ummu->cap.features & UMMU_FEAT_MTM)) {
		dev_warn(ummu->dev, "ummu mpam is not supported!\n");
		return -EINVAL;
	}

	if (partid > ummu->cap.mtm_id_max || pmg > ummu->cap.mtm_gp_max) {
		dev_err(ummu->dev,
			"ummu mpam rmid exceeds range: partid[0, %u] pmg[0, %u]\n",
			ummu->cap.mtm_id_max, ummu->cap.mtm_gp_max);
		return -ERANGE;
	}
	return 0;
}

static void ummu_set_tct_mtm(__le64 *tcte, u32 partid, u32 pmg)
{
	u64 val = le64_to_cpu(tcte[1]);

	val &= ~(TCT_ENT1_MTM_ID | TCT_ENT1_MTM_GP);
	val |= FIELD_PREP(TCT_ENT1_MTM_ID, partid);
	val |= FIELD_PREP(TCT_ENT1_MTM_GP, pmg);
	WRITE_ONCE(tcte[1], cpu_to_le64(val));
}

static int ummu_get_cfg_table_ptr(struct ummu_device *ummu,
				  u32 tid, __le64 **tcte)
{
	struct ummu_tct_desc_cfg *tct_cfg;

	tct_cfg = ummu->local_tct_cfg;
	*tcte = ummu_get_tcte_ptr(tct_cfg, tid);
	if (!(*tcte))
		return -ESPIPE;

	if (!(le64_to_cpu((*tcte)[0]) & TCT_ENT0_V))
		return -ENXIO;

	return 0;
}

int ummu_group_set_mpam(struct iommu_group *group, u16 partid, u8 pmg)
{
	struct iommu_domain *domain;
	struct ummu_domain *u_domain;
	struct ummu_device *ummu;
	u32 domain_type, tid;
	__le64 *tcte;
	int ret;

	domain = iommu_get_domain_for_group(group);
	domain_type = domain->type & IOMMU_DOMAIN_ALLOC_FLAGS;
	if (domain_type == IOMMU_DOMAIN_IDENTITY ||
		domain_type == IOMMU_DOMAIN_BLOCKED)
		return -EINVAL;

	u_domain = to_ummu_domain(iommu_to_agent_domain(domain));
	ummu = core_to_ummu_device(u_domain->base_domain.core_dev);

	ret = ummu_check_mpam_cap_info(ummu, partid, pmg);
	if (ret)
		return ret;

	tid = u_domain->base_domain.tid;
	ret = ummu_get_cfg_table_ptr(ummu, tid, &tcte);
	if (ret)
		return ret;

	/* write mpam info to tcte */
	ummu_set_tct_mtm(tcte, partid, pmg);
	ummu_sync_tct(ummu, 0, tid, true);

	/* It's likely that we'll want to use the new tcte soon */
	ummu_device_prefetch_cfg(ummu, 0, tid);
	pr_info("partid %d, pmg %d already matched\n", partid, pmg);
	return 0;
}

static int ummu_get_mpam_info(struct ummu_device *ummu,
			      u32 tid, u16 *partid, u8 *pmg)
{
	__le64 *tcte;
	u64 val;
	int ret;

	ret = ummu_get_cfg_table_ptr(ummu, tid, &tcte);
	if (ret)
		return ret;

	val = le64_to_cpu(tcte[1]);
	*partid = (int)FIELD_GET(TCT_ENT1_MTM_ID, val);
	*pmg = (int)FIELD_GET(TCT_ENT1_MTM_GP, val);

	return 0;
}

int ummu_group_get_mpam(struct iommu_group *group, u16 *partid, u8 *pmg)
{
	struct iommu_domain *domain;
	struct ummu_domain *u_domain;
	struct ummu_device *ummu;
	u32 domain_type;

	if (!partid || !pmg)
		return -ENXIO;

	domain = iommu_get_domain_for_group(group);
	domain_type = domain->type & IOMMU_DOMAIN_ALLOC_FLAGS;
	if (domain_type == IOMMU_DOMAIN_IDENTITY ||
	    domain_type == IOMMU_DOMAIN_BLOCKED)
		return -EINVAL;
	u_domain = to_ummu_domain(iommu_to_agent_domain(domain));
	ummu = core_to_ummu_device(u_domain->base_domain.core_dev);
	return ummu_get_mpam_info(ummu,
				  u_domain->base_domain.tid, partid, pmg);
}

int ummu_set_bypass_mpam(struct ummu_device *ummu, int partid, int pmg)
{
	void __iomem *bp_mtm_addr;
	u32 reg = 0;
	u32 val;
	int ret;

	ret = ummu_check_mpam_cap_info(ummu, partid, pmg);
	if (ret)
		return ret;

	bp_mtm_addr = ummu->base + UMMU_GBPA_MTM_CFG;
	reg |= FIELD_PREP(GBPA_TRANS_MTM_ID, partid);
	reg |= FIELD_PREP(GBPA_TRANS_MTM_GP, pmg);
	writel_relaxed(reg | GBPA_UPDATE_FLAG, bp_mtm_addr);
	ret = readl_relaxed_poll_timeout(bp_mtm_addr, val,
					 !(val & GBPA_UPDATE_FLAG), 1,
					 UMMU_REG_POLL_TIMEOUT_US);
	if (ret) {
		dev_err(ummu->dev, "ummu write bypass_mpam data timeout\n");
		return ret;
	}
	return 0;
}

int ummu_get_bypass_mpam(struct ummu_device *ummu, int *partid, int *pmg)
{
	void __iomem *bp_mtm_addr;
	u32 reg;

	if (!ummu || !partid || !pmg)
		return -EINVAL;

	bp_mtm_addr = ummu->base + UMMU_GBPA_MTM_CFG;
	reg = readl_relaxed(bp_mtm_addr);
	*partid = FIELD_GET(GBPA_TRANS_MTM_ID, reg);
	*pmg = FIELD_GET(GBPA_TRANS_MTM_GP, reg);
	return 0;
}

int ummu_set_uotr_mpam(struct ummu_device *ummu, int partid, int pmg)
{
	void __iomem *cr3_addr;
	u32 reg = 0;
	u32 val;
	int ret;

	ret = ummu_check_mpam_cap_info(ummu, partid, pmg);
	if (ret)
		return ret;

	cr3_addr = ummu->base + UMMU_CR3;
	reg |= FIELD_PREP(CR3_TRANS_MTM_ID, partid);
	reg |= FIELD_PREP(CR3_TRANS_MTM_GP, pmg);

	writel_relaxed(reg | CR3_UPDATE_FLAG, cr3_addr);
	ret = readl_relaxed_poll_timeout(cr3_addr, val, !(val & CR3_UPDATE_FLAG), 1,
					 UMMU_REG_POLL_TIMEOUT_US);
	if (ret) {
		dev_err(ummu->dev, "ummu write uotr_mpam data timeout\n");
		return ret;
	}
	return 0;
}

int ummu_get_uotr_mpam(struct ummu_device *ummu, int *partid, int *pmg)
{
	void __iomem *cr3_addr;
	u32 reg;

	if (!ummu || !partid || !pmg)
		return -EINVAL;

	cr3_addr = ummu->base + UMMU_CR3;
	reg = readl_relaxed(cr3_addr);
	*partid = FIELD_GET(CR3_TRANS_MTM_ID, reg);
	*pmg = FIELD_GET(CR3_TRANS_MTM_GP, reg);
	return 0;
}

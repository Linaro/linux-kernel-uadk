// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved.
 * Description: IOMMU API for UMMU
 */

#define pr_fmt(fmt) "UMMU: " fmt

#include <linux/iommu.h>
#include <uapi/linux/iommufd.h>
#include <linux/iova.h>
#include <ub/ubfi/ubfi.h>
#include <linux/slab.h>
#include <linux/io-pgtable.h>
#include <ub/ubus/ubus.h>
#include <linux/ummu_core.h>

#include "ummu.h"
#include "flush.h"
#include "logic_ummu/logic_ummu.h"
#include "perm_queue.h"
#include "page_table.h"
#include "cfg_table.h"
#include "perm_table.h"
#include "qos.h"
#include "queue.h"
#include "sva.h"

static bool ummu_capable(struct device *dev, enum iommu_cap cap)
{
	struct ummu_master *master =
		(struct ummu_master *)dev_iommu_priv_get(dev);
	u32 features;

	switch (cap) {
	case IOMMU_CAP_CACHE_COHERENCY:
		return master->ummu->cap.features & UMMU_FEAT_COHERENCY;
	case IOMMU_CAP_NOEXEC:
	case IOMMU_CAP_DEFERRED_FLUSH:
		return true;
	case IOMMU_CAP_DIRTY_TRACKING: {
		features = (UMMU_FEAT_HD | UMMU_FEAT_COHERENCY);
		return (master->ummu->cap.features & features) == features;
	}
	default:
		return false;
	}
}

struct ummu_domain *ummu_domain_alloc_helper(void)
{
	struct ummu_domain *u_domain;

	u_domain = kzalloc(sizeof(*u_domain), GFP_KERNEL);
	if (!u_domain)
		return NULL;

	u_domain->base_domain.tid = UMMU_INVALID_TID;
	u_domain->qid = UMMU_INVALID_QID;
	mutex_init(&u_domain->init_mutex);

	return u_domain;
}

static struct iommu_domain *ummu_domain_alloc(unsigned int type)
{
	struct ummu_domain *u_domain;

	switch (type) {
	case IOMMU_DOMAIN_DMA:
	case IOMMU_DOMAIN_DMA_FQ:
	case IOMMU_DOMAIN_BLOCKED:
	case IOMMU_DOMAIN_UNMANAGED:
		u_domain = ummu_domain_alloc_helper();
		if (!u_domain)
			return ERR_PTR(-ENOMEM);
		break;
	default:
		return ERR_PTR(-EINVAL);
	}

	return &u_domain->base_domain.domain;
}

static void ummu_domain_free(struct iommu_domain *domain)
{
	struct ummu_domain *u_domain = to_ummu_domain(domain);
	struct ummu_domain_cfgs *cfgs = &u_domain->cfgs;

	free_io_pgtable_ops(cfgs->pgtbl_ops);

	if (cfgs->stage == UMMU_DOMAIN_S1 &&
	    u_domain->base_domain.tid != UMMU_INVALID_TID &&
	    u_domain->base_domain.tid != UMMU_NO_TID)
		ummu_core_free_tid(u_domain->base_domain.core_dev,
					u_domain->base_domain.tid);

	kfree(u_domain);
}

static void set_dev_tid(struct device *dev, u32 tid)
{
	struct ub_entity *uent;

	if (dev_is_ub(dev)) {
		uent = to_ub_entity(dev);
		uent->tid = tid;
	}
}

static void ummu_detach_dev(struct ummu_master *master)
{
	struct ummu_domain *u_domain;
	struct iommu_domain *domain;
	u32 tid;
	int ret;

	/* get tid from dev */
	ret = ummu_get_tid(master->dev, NULL, &tid);
	if (ret || tid == UMMU_INVALID_TID)
		return;

	domain = ummu_core_get_domain_by_tid(master->dev, tid);
	if (!domain)
		return;

	domain = iommu_to_agent_domain(domain);
	if (!(domain->type & __IOMMU_DOMAIN_PAGING))
		return;

	u_domain = to_ummu_domain(domain);
	if (!u_domain->cfgs.s1_cfg.tct_cfg || tid != u_domain->base_domain.tid)
		return;

	if (tid != UMMU_NO_TID)
		ummu_write_tct_desc(core_to_ummu_device(
					u_domain->base_domain.core_dev),
					&u_domain->cfgs, true);

	pr_debug("detach device %s tid %u is called!\n", dev_name(master->dev),
		 tid);
}

static int ummu_domain_context_prepare(struct ummu_domain *ummu_domain)
{
	u32 features = core_to_ummu_device(ummu_domain->base_domain.core_dev)->cap.features;
	int ret;

	if (ummu_domain->cfgs.stage == UMMU_DOMAIN_S1) {
		if (!(features & UMMU_FEAT_TRANS_S1))
			return -EOPNOTSUPP;
	} else if (ummu_domain->cfgs.stage == UMMU_DOMAIN_S2) {
		if (!(features & UMMU_FEAT_TRANS_S2))
			return -EOPNOTSUPP;
	} else {
		return -EINVAL;
	}

	ret = ummu_domain_collect_pgtable(ummu_domain);
	if (ret == 0)
		ummu_domain->has_cfged = true;
	return ret;
}

static int ummu_domain_alloc_tid(struct ummu_domain *ummu_domain,
				 struct ummu_master *master)
{
	struct ummu_tid_param para = { .mode = MAPT_MODE_END };
	int ret;

	if (!ummu_check_dev_to_vm(master) &&
	    (ummu_domain->base_domain.tid == UMMU_INVALID_TID)) {
		ret = device_property_read_u32(master->dev, "assign-pasid", &para.assign_tid);
		if (ret)
			para.assign_tid = UMMU_INVALID_TID;

		if (para.assign_tid == UMMU_INVALID_TID)
			para.alloc_mode = TID_ALLOC_NORMAL;
		else
			para.alloc_mode = TID_ALLOC_ASSIGNED;

		para.device = master->dev;
		para.domain_type = ummu_domain->base_domain.domain.type;
		ret = ummu_core_alloc_tid(&master->ummu->core_dev, &para, &para.assign_tid);
		if (ret) {
			dev_err(master->dev, "alloc tid failed ret = %d!\n", ret);
			return ret;
		}
		ummu_domain->base_domain.tid = para.assign_tid;
	} else {
		if (ummu_domain->base_domain.tid == UMMU_INVALID_TID)
			ummu_domain->base_domain.tid = UMMU_NO_TID;
	}

	return 0;
}

static int ummu_domain_context_set(struct ummu_domain *ummu_domain,
				   struct ummu_master *master)
{
	struct ummu_tecte_data target;
	int ret;

	ret = ummu_set_domain_cfgs_tag(&ummu_domain->cfgs, master);
	if (ret)
		return ret;

	if (ummu_domain->cfgs.stage == UMMU_DOMAIN_S1) {
		ret = ummu_domain_alloc_tid(ummu_domain, master);
		if (ret)
			return ret;
		ummu_write_tct_desc(core_to_ummu_device(
					ummu_domain->base_domain.core_dev),
					&ummu_domain->cfgs, false);
		set_dev_tid(master->dev, ummu_domain->base_domain.tid);
	} else {
		ummu_build_s2_domain_tecte(ummu_domain, &target);
		ummu_device_write_tecte(core_to_ummu_device(
					ummu_domain->base_domain.core_dev),
					ummu_domain->cfgs.tecte_tag, &target);
	}
	return 0;
}

static int ummu_attach_dev(struct iommu_domain *domain, struct device *dev)
{
	struct ummu_domain *u_domain = to_ummu_domain(domain);
	struct ummu_master *master =
		(struct ummu_master *)dev_iommu_priv_get(dev);
	int ret;

	if (domain->type == IOMMU_DOMAIN_BLOCKED)
		return 0;

	/* if the pgtable has been set, clean up the data structures */
	ummu_detach_dev(master);

	guard(mutex)(&u_domain->init_mutex);
	if (!u_domain->has_cfged) {
		ret = ummu_domain_context_prepare(u_domain);
		if (ret) {
			pr_err("prepare domain ctx failed, ret = %d\n", ret);
			return ret;
		}
	}

	return ummu_domain_context_set(u_domain, master);
}

static int ummu_map_pages(struct iommu_domain *domain, unsigned long iova,
			  phys_addr_t paddr, size_t pgsize, size_t pgcount,
			  int prot, gfp_t gfp, size_t *mapped)
{
	struct ummu_domain *u_domain = to_ummu_domain(domain);
	struct io_pgtable_ops *ops = u_domain->cfgs.pgtbl_ops;

	if (unlikely(!ops || !ops->map_pages))
		return -ENODEV;

	return ops->map_pages(ops, iova, paddr, pgsize, pgcount, prot, gfp,
			      mapped);
}

static size_t ummu_unmap_pages(struct iommu_domain *domain, unsigned long iova,
			       size_t pgsize, size_t pgcount,
			       struct iommu_iotlb_gather *gather)
{
	struct ummu_domain *u_domain = to_ummu_domain(domain);
	struct io_pgtable_ops *ops = u_domain->cfgs.pgtbl_ops;

	if (unlikely(!ops || !ops->unmap_pages)) {
		pr_err("tid %u unmap pages cannot find valid ops!\n",
			u_domain->base_domain.tid);
		return 0;
	}

	return ops->unmap_pages(ops, iova, pgsize, pgcount, gather);
}

static phys_addr_t ummu_iova_to_phys(struct iommu_domain *domain,
				     dma_addr_t iova)
{
	struct io_pgtable_ops *ops = to_ummu_domain(domain)->cfgs.pgtbl_ops;

	if (!ops || !ops->iova_to_phys)
		return 0;

	return ops->iova_to_phys(ops, iova);
}

static struct iommu_device *ummu_probe_device(struct device *dev)
{
	struct ummu_master *master;
	struct ummu_device *ummu;

	master = kzalloc(sizeof(*master), GFP_KERNEL);
	if (!master)
		return ERR_PTR(-ENOMEM);

	ummu = (struct ummu_device *)dev_iommu_priv_get(dev);
	master->dev = dev;
	master->ummu = ummu;
	refcount_set(&master->ksva_ref, 1);
	refcount_set(&master->sva_ref, 1);
	dev_iommu_priv_set(dev, master);
	pr_debug("ummu probe device %s successful!\n", dev_name(dev));
	return &ummu->core_dev.iommu;
}

static void ummu_release_device(struct device *dev)
{
	struct ummu_master *master =
		(struct ummu_master *)dev_iommu_priv_get(dev);
	u32 tid;
	int ret;

	ret = ummu_get_tid(dev, NULL, &tid);
	if (ret || tid == UMMU_INVALID_TID)
		return;

	ummu_detach_dev(master);
	set_dev_tid(dev, UMMU_INVALID_TID);
	WARN_ON(refcount_read(&master->sva_ref) > 1);
	WARN_ON(refcount_read(&master->ksva_ref) > 1);
	dev_iommu_priv_set(dev, NULL);
	kfree(master);
}

static void ummu_get_resv_regions(struct device *device, struct list_head *head)
{
	struct iommu_resv_region *region;
	int prot = IOMMU_WRITE | IOMMU_NOEXEC | IOMMU_MMIO;

	region = iommu_alloc_resv_region(MSI_IOVA_BASE, MSI_IOVA_LENGTH, prot,
					 IOMMU_RESV_SW_MSI, GFP_KERNEL);
	if (!region)
		return;

	list_add_tail(&region->list, head);

	ubrt_iommu_get_resv_regions(device, head);
}

static struct iommu_group *ummu_device_group(struct device *dev)
{
	return generic_device_group(dev);
}

static int ummu_dev_enable_feat(struct device *dev,
				enum iommu_dev_features feat)
{
	struct ummu_master *master =
		(struct ummu_master *)dev_iommu_priv_get(dev);

	if (!master) {
		pr_err("get invalid dev!\n");
		return -ENODEV;
	}

	switch (feat) {
	case IOMMU_DEV_FEAT_IOPF:
		return -EOPNOTSUPP;
	case IOMMU_DEV_FEAT_SVA:
	case IOMMU_DEV_FEAT_KSVA:
		return ummu_master_enable_sva(master, feat);
	default:
		return -EINVAL;
	}
}

static int ummu_dev_disable_feat(struct device *dev,
				 enum iommu_dev_features feat)
{
	struct ummu_master *master =
		(struct ummu_master *)dev_iommu_priv_get(dev);

	if (!master) {
		pr_err("get invalid dev!\n");
		return -ENODEV;
	}

	switch (feat) {
	case IOMMU_DEV_FEAT_IOPF:
		return -EOPNOTSUPP;
	case IOMMU_DEV_FEAT_SVA:
	case IOMMU_DEV_FEAT_KSVA:
		return ummu_master_disable_sva(master, feat);
	default:
		return -EINVAL;
	}
}

static int ummu_def_domain_type(struct device *dev)
{
	if (iommu_default_passthrough())
		return IOMMU_DOMAIN_IDENTITY;
	return 0;
}

static void ummu_remove_dev_pasid(struct device *dev,
			ioasid_t id, struct iommu_domain *domain)
{
	struct ummu_domain *u_domain;
	struct ummu_master *master;

	master = (struct ummu_master *)dev_iommu_priv_get(dev);
	u_domain = to_ummu_domain(domain);
	if (domain->type == IOMMU_DOMAIN_SVA)
		ummu_sva_domain_remove_tid(u_domain, master, id);
}

const struct iommu_domain_ops default_domain_ops = {
	.attach_dev = ummu_attach_dev,
	.map_pages = ummu_map_pages,
	.unmap_pages = ummu_unmap_pages,
	.flush_iotlb_all = ummu_flush_iotlb_all,
	.iotlb_sync = ummu_iotlb_sync,
	.iova_to_phys = ummu_iova_to_phys,
	.free = ummu_domain_free,
};

struct iommu_ops ummu_iommu_ops = {
	.capable = ummu_capable,
	.domain_alloc = ummu_domain_alloc,
	.domain_alloc_sva = ummu_domain_alloc_sva,
	.probe_device = ummu_probe_device,
	.release_device = ummu_release_device,
	.device_group = ummu_device_group,
	.get_resv_regions = ummu_get_resv_regions,
	.dev_enable_feat = ummu_dev_enable_feat,
	.dev_disable_feat = ummu_dev_disable_feat,
	.def_domain_type = ummu_def_domain_type,
	.remove_dev_pasid = ummu_remove_dev_pasid,
	.get_group_qos_params = ummu_group_get_mpam,
	.set_group_qos_params = ummu_group_set_mpam,
	.default_domain_ops = &default_domain_ops,
	.pgsize_bitmap = -1UL,
	.owner = THIS_MODULE,
};

static int ummu_get_resource(struct ummu_base_domain *base_domain,
			     struct resource_args *args)
{
	struct ummu_domain *u_domain = to_ummu_domain(&base_domain->domain);
	struct ummu_device *ummu = core_to_ummu_device(base_domain->core_dev);
	int ret;

	switch (args->type) {
	case UMMU_BLOCK:
		ret = ummu_alloc_mapt_blk_mem(u_domain, &args->block);
		pr_debug("tid %u alloc mapt block %s!\n",
			 u_domain->base_domain.tid, ret ? "failed" : "successful");
		break;
	case UMMU_QUEUE:
		if (u_domain->qid == UMMU_INVALID_QID) {
			ret = ummu_domain_config_permq(u_domain);
			if (ret)
				return ret;
		}
		ret = ummu_get_permq_resource(ummu, u_domain->qid, &args->queue);
		break;
	case UMMU_TID_RES:
		ret = ummu_get_tid_res(&args->tid_res);
		if (ret)
			return ret;

		if (ummu_get_mapt_blk_exp())
			args->tid_res.hw_cap |= HW_CAP_EXPAN;
		args->tid_res.blk_exp_size = ummu_get_mapt_base_blk_size();
		break;
	default:
		ret = -EINVAL;
		break;
	}
	pr_debug("driver base domain 0x%pK get resource with ret=%d.\n",
		 base_domain, ret);
	return ret;
}

static void ummu_put_resource(struct ummu_base_domain *base_domain,
			      struct resource_args *args)
{
	struct ummu_domain *u_domain = to_ummu_domain(&base_domain->domain);

	switch (args->type) {
	case UMMU_BLOCK:
		ummu_release_mapt_blk_mem(u_domain, &args->block);
		break;
	case UMMU_QUEUE:
		ummu_release_permq_resource(u_domain);
		break;
	default:
		break;
	}
}

static void ummu_cfg_sync(struct ummu_base_domain *base_domain)
{
	struct ummu_domain *u_domain;
	struct ummu_device *ummu;
	u32 tag, tid;

	u_domain = to_ummu_domain(&base_domain->domain);
	ummu = core_to_ummu_device(u_domain->base_domain.core_dev);
	tag = u_domain->cfgs.tecte_tag;
	tid = u_domain->base_domain.tid;

	ummu_sync_tct(ummu, tag, tid, true);
}

static void ummu_cfg_sync_all(struct ummu_base_domain *base_domain)
{
	struct ummu_domain *u_domain = to_ummu_domain(&base_domain->domain);
	struct ummu_device *ummu = core_to_ummu_device(base_domain->core_dev);
	u32 tag = u_domain->cfgs.tecte_tag;

	ummu_device_sync_tect(ummu, tag);
}

static int ummu_sync_dom_cfg(struct ummu_base_domain *src,
			     struct ummu_base_domain *dst,
			     enum ummu_dom_cfg_sync_type type)
{
	struct ummu_domain *src_domain, *dst_domain;

	switch (type) {
	case SYNC_DOM_ALL_CFG:
		src_domain = to_ummu_domain(&src->domain);
		dst_domain = to_ummu_domain(&dst->domain);
		memcpy(&dst_domain->cfgs, &src_domain->cfgs,
		       sizeof(struct ummu_domain_cfgs));
		dst_domain->base_domain.tid = src_domain->base_domain.tid;
		break;
	case SYNC_DOM_MUTI_CFG:
		src_domain = to_ummu_domain(&src->domain);
		dst_domain = to_ummu_domain(&dst->domain);
		dst_domain->base_domain.tid = src_domain->base_domain.tid;
		dst_domain->cfgs.tecte_tag = src_domain->cfgs.tecte_tag;
		dst_domain->cfgs.stage = src_domain->cfgs.stage;
		break;
	case SYNC_CLEAR_DOM_ALL_CFG:
		dst_domain = to_ummu_domain(&dst->domain);
		memset(&dst_domain->cfgs, 0, sizeof(dst_domain->cfgs));
		dst_domain->base_domain.tid = UMMU_INVALID_TID;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int ummu_invalidate_cfg(struct ummu_base_domain *base_domain)
{
	ummu_sva_tcte_invalidate(to_ummu_domain(&base_domain->domain));
	return 0;
}

const struct ummu_core_ops ummu_ops = {
	.cfg_sync_all = ummu_cfg_sync_all,
	.cfg_sync = ummu_cfg_sync,
	.get_resource = ummu_get_resource,
	.put_resource = ummu_put_resource,
	.add_eid = ummu_add_eid,
	.del_eid = ummu_del_eid,
	.invalidate_cfg = ummu_invalidate_cfg,
};

const struct ummu_device_helper ummu_helper = {
	.sync_tlb = ummu_non_agent_iotlb_sync,
	.sync_dom_cfg = ummu_sync_dom_cfg,
};

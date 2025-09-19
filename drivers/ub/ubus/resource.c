// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) HiSilicon Technologies Co., Ltd. 2025. All rights reserved.
 */

#define pr_fmt(fmt)	"ubus resource: " fmt

#include <linux/io.h>
#include <linux/mm.h>

#include "ubus.h"
#include "msg.h"
#include "resource.h"

static const struct vm_operations_struct ub_phys_vm_ops = {
	.access = generic_access_phys,
};

int ub_mmap_resource_range(struct ub_entity *uent, unsigned long idx,
			   struct vm_area_struct *vma, int write_combine)
{
	resource_size_t size;

	size = ((ub_resource_len(uent, idx) - 1) >> PAGE_SHIFT) + 1;
	if (vma->vm_pgoff + vma_pages(vma) > size)
		return -EINVAL;

	if (write_combine)
		vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);
	else
		vma->vm_page_prot = pgprot_device(vma->vm_page_prot);

	vma->vm_pgoff += (ub_resource_start(uent, idx) >> PAGE_SHIFT);
	vma->vm_ops = &ub_phys_vm_ops;

	return io_remap_pfn_range(vma, vma->vm_start, vma->vm_pgoff,
				  vma->vm_end - vma->vm_start,
				  vma->vm_page_prot);
}

static void __iomem *ub_iomap_range(struct ub_entity *uent, int res_id,
				    unsigned long offset, unsigned long maxlen,
				    bool is_wc)
{
	resource_size_t start = ub_resource_start(uent, res_id);
	resource_size_t len = ub_resource_len(uent, res_id);
	unsigned long flags = ub_resource_flags(uent, res_id);

	if (len <= offset || !start)
		return NULL;
	len -= offset;
	start += offset;
	if (maxlen && len > maxlen)
		len = maxlen;
	if (!(flags & IORESOURCE_MEM))
		return NULL;

	if (is_wc)
		return ioremap_wc(start, len);
	else
		return ioremap(start, len);
}

void __iomem *ub_iomap(struct ub_entity *uent, int res_id, unsigned long maxlen)
{
	if (res_id >= MAX_UB_RES_NUM || !uent)
		return NULL;

	return ub_iomap_range(uent, res_id, 0, maxlen, false);
}
EXPORT_SYMBOL_GPL(ub_iomap);

void __iomem *ub_iomap_wc(struct ub_entity *uent, int res_id, unsigned long maxlen)
{
	if (res_id >= MAX_UB_RES_NUM || !uent)
		return NULL;

	return ub_iomap_range(uent, res_id, 0, maxlen, true);
}
EXPORT_SYMBOL_GPL(ub_iomap_wc);

void ub_iounmap(void __iomem *addr)
{
	iounmap(addr);
}
EXPORT_SYMBOL_GPL(ub_iounmap);

static int ub_read_ubba_reg(struct ub_entity *uent, size_t pg_size, int idx)
{
	u32 addr_l_reg[MAX_UB_RES_NUM] = { UB_ERS0_UBBA_L, UB_ERS1_UBBA_L,
					   UB_ERS2_UBBA_L };
	u32 addr_h_reg[MAX_UB_RES_NUM] = { UB_ERS0_UBBA_H, UB_ERS1_UBBA_H,
					   UB_ERS2_UBBA_H };
	u32 ss_reg[MAX_UB_RES_NUM] = { UB_ERS0_SS, UB_ERS1_SS, UB_ERS2_SS };
	u32 hpa_l = 0, hpa_h = 0, size = 0;
	int ret;

	ret = ub_cfg_read_dword(uent, addr_l_reg[idx], &hpa_l);
	ret |= ub_cfg_read_dword(uent, addr_h_reg[idx], &hpa_h);
	ret |= ub_cfg_read_dword(uent, ss_reg[idx], &size);
	if (ret) {
		ub_err(uent, "read reg failed when request HPA resource\n");
		return -EINVAL;
	} else if (size == 0) {
		ub_err(uent, "size is 0 when request HPA resource\n");
		return -EINVAL;
	}

	uent->zone[idx].res.start = ubba_gen(hpa_h, hpa_l);
	uent->zone[idx].res.end =
		uent->zone[idx].res.start + ((u64)size * pg_size - 1);

	uent->zone[idx].res.flags = IORESOURCE_MEM;
	uent->zone[idx].res.name = ub_name(uent);
	uent->zone[idx].ubba_used = 1;

	ub_info(uent, "mmio idx %d, %pR\n", idx, &uent->zone[idx].res);
	return 0;
}

static int ub_read_sa_reg(struct ub_entity *uent, size_t pg_size, int idx)
{
	u32 sa_l_reg[MAX_UB_RES_NUM] = { UB_ERS0_SA_L, UB_ERS1_SA_L,
					 UB_ERS2_SA_L };
	u32 sa_h_reg[MAX_UB_RES_NUM] = { UB_ERS0_SA_H, UB_ERS1_SA_H,
					 UB_ERS2_SA_H };
	u32 ss_reg[MAX_UB_RES_NUM] = { UB_ERS0_SS, UB_ERS1_SS, UB_ERS2_SS };
	u32 sa_l = 0, sa_h = 0, size = 0;
	int ret;

	ret = ub_cfg_read_dword(uent, sa_l_reg[idx], &sa_l);
	ret |= ub_cfg_read_dword(uent, sa_h_reg[idx], &sa_h);
	ret |= ub_cfg_read_dword(uent, ss_reg[idx], &size);
	if (ret) {
		ub_err(uent, "read reg failed when request SA resource\n");
		return -EINVAL;
	} else if (size == 0) {
		ub_err(uent, "size is 0 when request SA resource\n");
		return -EINVAL;
	}

	/* Shift left 32 bits to get upper-bit address */
	uent->zone[idx].region.start = ((u64)sa_h << 32) | sa_l;
	uent->zone[idx].region.size = size * pg_size;
	uent->zone[idx].res.flags = IORESOURCE_MEM;
	uent->zone[idx].sa_used = 1;

	return 0;
}

static int ub_entity_read_mmio_idx(struct ub_entity *uent, int idx)
{
	struct ub_entity *pue;
	size_t pg_size = 0;

	/* ue to its mue, mue to Entity0, Entity0 use itself */
	pue = uent->pue;
	if (pue->entity_idx) /* to ensure Entity0 uent */
		pue = pue->pue;

	(void)ub_cfg_read_byte(uent, UB_SYS_PGS, (u8 *)&pg_size);
	if ((u8)pg_size & UB_SYS_PGS_SIZE)
		pg_size = SZ_64K;
	else
		pg_size = SZ_4K;

	if (pue->zone[idx].ubba_used)
		return ub_read_ubba_reg(uent, pg_size, idx);

	if (pue->zone[idx].sa_used)
		return ub_read_sa_reg(uent, pg_size, idx);

	return -EPERM;
}

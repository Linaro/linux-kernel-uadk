// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2012 Red Hat, Inc.  All rights reserved.
 * Author: Alex Williamson <alex.williamson@redhat.com>
 *
 * Derived from original vfio:
 * Copyright 2010 Cisco Systems, Inc.  All rights reserved.
 * Author: Tom Lyon, pugs@cisco.com
 *
 * Thanks to Alex Williamson and Tom Lyon for their original
 * vfio implementation.
 *
 * Copyright (c) HiSilicon Technologies Co., Ltd. 2025. All rights reserved.
 */

#define pr_fmt(fmt) "vfio ub: " fmt

#include <linux/module.h>
#include <linux/vfio.h>
#include <linux/types.h>
#include <linux/eventfd.h>

#include "vfio_ub_private.h"

static bool is_ub_entity_support_used(struct ub_entity *uent)
{
	if (!((uent_type(uent) == UB_TYPE_CONTROLLER ||
	    uent_type(uent) == UB_TYPE_ICONTROLLER) &&
	    uent_base_code(uent) != UB_BASE_CODE_BUS_CONTROLLER))
		return false;

	if (!list_empty(&uent->ue_list))
		return false;

	return true;
}

int vfio_ub_core_register_device(struct vfio_ub_core_device *vdev)
{
	struct ub_entity *uent = vdev->uent;
	struct device *dev = &uent->dev;

	/* Drivers must set the vfio_ub_core_device to their drvdata */
	if (WARN_ON(vdev != dev_get_drvdata(dev)))
		return -EINVAL;

	if (!is_ub_entity_support_used(uent)) {
		ub_err(uent, "Cannot bind to vfio driver\n");
		return -EBUSY;
	}

	return vfio_register_group_dev(&vdev->vdev);
}

void vfio_ub_core_unregister_device(struct vfio_ub_core_device *vdev)
{
	vfio_unregister_group_dev(&vdev->vdev);
}

static int vfio_usi_info_init(struct vfio_ub_core_device *vdev)
{
	struct ub_entity *uent = vdev->uent;
	u32 low32;
	u32 high32;
	u16 num;
	int ret;

	/* when device supports int type2, read usi info from cfgspace */
	if (uent->no_intr == 0 && uent->intr_type1 == 0) {
		ret = ub_cfg_read_dword(vdev->uent, UB_INT_VECTOR_TBL_SA_L, &low32);
		ret |= ub_cfg_read_dword(vdev->uent, UB_INT_VECTOR_TBL_SA_H, &high32);
		ret |= ub_cfg_read_word(vdev->uent, UB_NUM_OF_INTR_VECTOR_TBL, &num);
		if (ret)
			return -EFAULT;

		vdev->usi_vector_offset = (u64)high32 << 32 | low32;
		vdev->usi_vector_size = (num + 1) * UB_INTR_VECTOR_ENTRY_SIZE;

		ret = ub_cfg_read_dword(vdev->uent, UB_INT_ADDR_TBL_SA_L, &low32);
		ret |= ub_cfg_read_dword(vdev->uent, UB_INT_ADDR_TBL_SA_H, &high32);
		ret |= ub_cfg_read_word(vdev->uent, UB_NUM_OF_INTR_ADDR_TBL, &num);
		if (ret)
			return -EFAULT;

		vdev->usi_addr_offset = (u64)high32 << 32 | low32;
		vdev->usi_addr_size = (num + 1) * UB_INTR_ADDR_ENTRY_SIZE;
	} else {
		vdev->usi_vector_offset = 0;
		vdev->usi_vector_size = 0;
		vdev->usi_addr_offset = 0;
		vdev->usi_addr_size = 0;
	}

	return 0;
}

static int vfio_ub_enable(struct vfio_ub_core_device *vdev)
{
	int ret;

	vdev->num_ext_irqs = 0;
	vdev->num_regions = 0;
	vdev->num_vendor_irqs = 0;
	vdev->num_vendor_regions = 0;
	vdev->reset_works = 1; /* we assume ub support ELR for now. */

	ret = vfio_usi_info_init(vdev);
	if (ret)
		return ret;

	return vfio_ub_config_init(vdev);
}

static void vfio_ub_disable(struct vfio_ub_core_device *vdev)
{
	vfio_ub_config_uninit(vdev);

	/* clear device caches when vm exit or crash */
	if (vdev->reset_works)
		ub_reset_entity(vdev->uent);
}

int vfio_ub_core_open_device(struct vfio_device *core_vdev)
{
	struct vfio_ub_core_device *vdev = container_of(core_vdev,
					   struct vfio_ub_core_device, vdev);

	return vfio_ub_enable(vdev);
}

void vfio_ub_core_close_device(struct vfio_device *core_vdev)
{
	struct vfio_ub_core_device *vdev = container_of(core_vdev,
					   struct vfio_ub_core_device, vdev);

	vfio_ub_disable(vdev);
	vfio_ub_unset_resmap(vdev);

	mutex_lock(&vdev->igate);
	if (vdev->req_trigger) {
		eventfd_ctx_put(vdev->req_trigger);
		vdev->req_trigger = NULL;
	}
	mutex_unlock(&vdev->igate);
}

static ssize_t vfio_ub_rw(struct vfio_ub_core_device *vdev, char __user *buf, size_t count,
			  loff_t *ppos, bool iswrite)
{
	unsigned int index = VFIO_UB_OFFSET_TO_INDEX(*ppos);

	if (index >= (VFIO_UB_NUM_REGIONS + vdev->num_regions))
		return -EINVAL;

	switch (index) {
	case VFIO_UB_CONFIG_REGION_INDEX:
		return vfio_ub_config_rw(vdev, buf, count, ppos, iswrite);
	case VFIO_UB_REGION0_INDEX:
	case VFIO_UB_REGION1_INDEX:
	case VFIO_UB_REGION2_INDEX:
		return vfio_ub_res_rw(vdev, buf, count, ppos, iswrite);
	default:
		return -EINVAL;
	}
}

ssize_t vfio_ub_core_read(struct vfio_device *core_vdev, char __user *buf, size_t count,
			  loff_t *ppos)
{
	struct vfio_ub_core_device *vdev =
		container_of(core_vdev, struct vfio_ub_core_device, vdev);

	if (!count)
		return 0;

	return vfio_ub_rw(vdev, buf, count, ppos, false);
}

ssize_t vfio_ub_core_write(struct vfio_device *core_vdev, const char __user *buf,
			   size_t count, loff_t *ppos)
{
	struct vfio_ub_core_device *vdev =
		container_of(core_vdev, struct vfio_ub_core_device, vdev);

	if (!count)
		return 0;

	return vfio_ub_rw(vdev, (char __user *)buf, count, ppos, true);
}

int vfio_ub_core_mmap(struct vfio_device *core_vdev, struct vm_area_struct *vma)
{
	struct vfio_ub_core_device *vdev =
		container_of(core_vdev, struct vfio_ub_core_device, vdev);
	u64 phys_len, req_len, pgoff, req_start;
	struct ub_entity *uent = vdev->uent;
	unsigned int index;

	if (vma->vm_end < vma->vm_start)
		return -EINVAL;

	/*
	 * VM_SHARED means user's changing to mmio can immediately arrive ub device,
	 * or it is unspecified.
	 */
	if ((vma->vm_flags & VM_SHARED) == 0)
		return -EINVAL;

	index = vma->vm_pgoff >> (VFIO_UB_OFFSET_SHIFT - PAGE_SHIFT);

	if (index > VFIO_UB_REGION2_INDEX)
		return -EINVAL;

	phys_len = PAGE_ALIGN(ub_resource_len(uent, index));
	req_len = vma->vm_end - vma->vm_start;
	pgoff = vma->vm_pgoff & ((1U << (VFIO_UB_OFFSET_SHIFT - PAGE_SHIFT)) - 1);
	req_start = pgoff << PAGE_SHIFT;

	if (req_start + req_len > phys_len)
		return -EINVAL;

	vm_flags_set(vma, VM_IO | VM_DONTCOPY | VM_DONTEXPAND | VM_DONTDUMP |
		     VM_PFNMAP | VM_WIPEONFORK);
	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
	vma->vm_pgoff = (ub_resource_start(uent, index) >> PAGE_SHIFT) + pgoff;
	if (remap_pfn_range(vma, vma->vm_start, vma->vm_pgoff,
		vma->vm_end - vma->vm_start, vma->vm_page_prot))
		return -EAGAIN;

	return 0;
}

int vfio_ub_core_init_dev(struct vfio_device *core_vdev)
{
	struct vfio_ub_core_device *vdev =
		container_of(core_vdev, struct vfio_ub_core_device, vdev);

	vdev->uent = to_ub_entity(core_vdev->dev);
	mutex_init(&vdev->igate);
	return 0;
}

void vfio_ub_core_release_dev(struct vfio_device *core_vdev)
{
	struct vfio_ub_core_device *vdev =
		container_of(core_vdev, struct vfio_ub_core_device, vdev);

	mutex_destroy(&vdev->igate);
}

void vfio_ub_core_disable_all(struct vfio_ub_core_device *vdev)
{
	struct ub_entity *uent = vdev->uent;

	if (!list_empty(&uent->ue_list))
		ub_disable_entities(uent);
}

// SPDX-License-Identifier: GPL-2.0
/*
 * Helpers for IOMMU drivers implementing SVA
 */
#include <linux/mmu_context.h>
#include <linux/mutex.h>
#include <linux/sched/mm.h>
#include <linux/iommu.h>

#include "iommu-priv.h"

static DEFINE_MUTEX(iommu_sva_lock);

/* Allocate a PASID for the mm within range (inclusive) */
static int iommu_sva_alloc_pasid(struct mm_struct *mm, struct device *dev)
{
	ioasid_t pasid;
	int ret = 0;

	if (!arch_pgtable_dma_compat(mm))
		return -EBUSY;

	mutex_lock(&iommu_sva_lock);
	/* Is a PASID already associated with this mm? */
	if (mm_valid_pasid(mm)) {
		if (mm->pasid >= dev->iommu->max_pasids)
			ret = -EOVERFLOW;
		goto out;
	}

	pasid = iommu_alloc_global_pasid(dev);
	if (pasid == IOMMU_PASID_INVALID) {
		ret = -ENOSPC;
		goto out;
	}
	mm->pasid = pasid;
	ret = 0;
out:
	mutex_unlock(&iommu_sva_lock);
	return ret;
}

/**
 * iommu_sva_bind_device() - Bind a process address space to a device
 * @dev: the device
 * @mm: the mm to bind, caller must hold a reference to mm_users
 *
 * Create a bond between device and address space, allowing the device to
 * access the mm using the PASID returned by iommu_sva_get_pasid(). If a
 * bond already exists between @device and @mm, an additional internal
 * reference is taken. Caller must call iommu_sva_unbind_device()
 * to release each reference.
 *
 * iommu_dev_enable_feature(dev, IOMMU_DEV_FEAT_SVA) must be called first, to
 * initialize the required SVA features.
 *
 * On error, returns an ERR_PTR value.
 */
struct iommu_sva *iommu_sva_bind_device(struct device *dev, struct mm_struct *mm)
{
	struct iommu_domain *domain;
	struct iommu_sva *handle;
	int ret;

	/* Allocate mm->pasid if necessary. */
	ret = iommu_sva_alloc_pasid(mm, dev);
	if (ret)
		return ERR_PTR(ret);

	handle = kzalloc(sizeof(*handle), GFP_KERNEL);
	if (!handle)
		return ERR_PTR(-ENOMEM);

	mutex_lock(&iommu_sva_lock);
	/* Search for an existing domain. */
	domain = iommu_get_domain_for_dev_pasid(dev, mm->pasid,
						IOMMU_DOMAIN_SVA);
	if (IS_ERR(domain)) {
		ret = PTR_ERR(domain);
		goto out_unlock;
	}

	if (domain) {
		domain->users++;
		goto out;
	}

	/* Allocate a new domain and set it on device pasid. */
	domain = iommu_sva_domain_alloc(dev, mm);
	if (IS_ERR(domain)) {
		ret = PTR_ERR(domain);
		goto out_unlock;
	}

	ret = iommu_attach_device_pasid(domain, dev, mm->pasid);
	if (ret)
		goto out_free_domain;
	domain->users = 1;
out:
	mutex_unlock(&iommu_sva_lock);
	handle->dev = dev;
	handle->domain = domain;

	return handle;

out_free_domain:
	iommu_domain_free(domain);
out_unlock:
	mutex_unlock(&iommu_sva_lock);
	kfree(handle);

	return ERR_PTR(ret);
}
EXPORT_SYMBOL_GPL(iommu_sva_bind_device);

/**
 * iommu_sva_unbind_device() - Remove a bond created with iommu_sva_bind_device
 * @handle: the handle returned by iommu_sva_bind_device()
 *
 * Put reference to a bond between device and address space. The device should
 * not be issuing any more transaction for this PASID. All outstanding page
 * requests for this PASID must have been flushed to the IOMMU.
 */
void iommu_sva_unbind_device(struct iommu_sva *handle)
{
	struct iommu_domain *domain = handle->domain;
	ioasid_t pasid = domain->mm->pasid;
	struct device *dev = handle->dev;

	mutex_lock(&iommu_sva_lock);
	if (--domain->users == 0) {
		iommu_detach_device_pasid(domain, dev, pasid);
		iommu_domain_free(domain);
	}
	mutex_unlock(&iommu_sva_lock);
	kfree(handle);
}
EXPORT_SYMBOL_GPL(iommu_sva_unbind_device);

u32 iommu_sva_get_pasid(struct iommu_sva *handle)
{
	struct iommu_domain *domain = handle->domain;

	return domain->mm->pasid;
}
EXPORT_SYMBOL_GPL(iommu_sva_get_pasid);

void mm_pasid_drop(struct mm_struct *mm)
{
	if (likely(!mm_valid_pasid(mm)))
		return;

	iommu_free_global_pasid(mm->pasid);
}

/*
 * I/O page fault handler for SVA
 */
static enum iommu_page_response_code
iommu_sva_handle_mm(struct iommu_fault *fault, struct mm_struct *mm)
{
	vm_fault_t ret;
	struct vm_area_struct *vma;
	unsigned int access_flags = 0;
	unsigned int fault_flags = FAULT_FLAG_REMOTE;
	struct iommu_fault_page_request *prm = &fault->prm;
	enum iommu_page_response_code status = IOMMU_PAGE_RESP_INVALID;

	if (!(prm->flags & IOMMU_FAULT_PAGE_REQUEST_PASID_VALID))
		return status;

	if (!mmget_not_zero(mm))
		return status;

	mmap_read_lock(mm);

	vma = vma_lookup(mm, prm->addr);
	if (!vma)
		/* Unmapped area */
		goto out_put_mm;

	if (prm->perm & IOMMU_FAULT_PERM_READ)
		access_flags |= VM_READ;

	if (prm->perm & IOMMU_FAULT_PERM_WRITE) {
		access_flags |= VM_WRITE;
		fault_flags |= FAULT_FLAG_WRITE;
	}

	if (prm->perm & IOMMU_FAULT_PERM_EXEC) {
		access_flags |= VM_EXEC;
		fault_flags |= FAULT_FLAG_INSTRUCTION;
	}

	if (!(prm->perm & IOMMU_FAULT_PERM_PRIV))
		fault_flags |= FAULT_FLAG_USER;

	if (access_flags & ~vma->vm_flags)
		/* Access fault */
		goto out_put_mm;

	ret = handle_mm_fault(vma, prm->addr, fault_flags, NULL);
	status = ret & VM_FAULT_ERROR ? IOMMU_PAGE_RESP_INVALID :
		IOMMU_PAGE_RESP_SUCCESS;

out_put_mm:
	mmap_read_unlock(mm);
	mmput(mm);

	return status;
}

static void iommu_sva_handle_iopf(struct work_struct *work)
{
	struct iopf_fault *iopf;
	struct iopf_group *group;
	enum iommu_page_response_code status = IOMMU_PAGE_RESP_SUCCESS;

	group = container_of(work, struct iopf_group, work);
	list_for_each_entry(iopf, &group->faults, list) {
		/*
		 * For the moment, errors are sticky: don't handle subsequent
		 * faults in the group if there is an error.
		 */
		if (status != IOMMU_PAGE_RESP_SUCCESS)
			break;

		status = iommu_sva_handle_mm(&iopf->fault, group->domain->mm);
	}

	iopf_group_response(group, status);
	iopf_free_group(group);
}

static int iommu_sva_iopf_handler(struct iopf_group *group)
{
	struct iommu_fault_param *fault_param = group->dev->iommu->fault_param;

	INIT_WORK(&group->work, iommu_sva_handle_iopf);
	if (!queue_work(fault_param->queue->wq, &group->work))
		return -EBUSY;

	return 0;
}

struct iommu_domain *iommu_sva_domain_alloc(struct device *dev,
					    struct mm_struct *mm)
{
	const struct iommu_ops *ops = dev_iommu_ops(dev);
	struct iommu_domain *domain;

	if (ops->domain_alloc_sva) {
		domain = ops->domain_alloc_sva(dev, mm);
		if (IS_ERR(domain))
			return domain;
	} else {
		domain = ops->domain_alloc(IOMMU_DOMAIN_SVA);
		if (!domain)
			return ERR_PTR(-ENOMEM);
	}

	domain->type = IOMMU_DOMAIN_SVA;
	mmgrab(mm);
	domain->mm = mm;
	domain->owner = ops;
	domain->iopf_handler = iommu_sva_iopf_handler;

	return domain;
}

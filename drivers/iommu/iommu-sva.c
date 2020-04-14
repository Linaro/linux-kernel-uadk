// SPDX-License-Identifier: GPL-2.0
/*
 * Manage PASIDs and bind process address spaces to devices.
 *
 * Copyright (C) 2020 ARM Ltd.
 */

#include <linux/debugfs.h>
#include <linux/idr.h>
#include <linux/ioasid.h>
#include <linux/iommu.h>
#include <linux/module.h>
#include <linux/sched/mm.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

#include <trace/events/iommu.h>

#include "iommu-sva.h"

static atomic_t sva_nr_mms = ATOMIC_INIT(0);

/**
 * DOC: io_mm model
 *
 * The io_mm keeps track of process address spaces shared between CPU and IOMMU.
 * The following example illustrates the relation between structures
 * iommu_domain, io_mm and iommu_sva. The iommu_sva struct is a bond between
 * io_mm and device. A device can have multiple io_mm and an io_mm may be bound
 * to multiple devices.
 *              ___________________________
 *             |  IOMMU domain A           |
 *             |  ________________         |
 *             | |  IOMMU group   |        |
 *             | |                |        |
 *             | |   dev 00:00.0 ----+------- bond 1 --- io_mm X
 *             | |________________|   \    |
 *             |                       '----- bond 2 ---.
 *             |___________________________|             \
 *              ___________________________               \
 *             |  IOMMU domain B           |             io_mm Y
 *             |  ________________         |             / /
 *             | |  IOMMU group   |        |            / /
 *             | |                |        |           / /
 *             | |   dev 00:01.0 ------------ bond 3 -' /
 *             | |   dev 00:01.1 ------------ bond 4 --'
 *             | |________________|        |
 *             |___________________________|
 *
 * In this example, device 00:00.0 is in domain A, devices 00:01.* are in domain
 * B. All devices within the same domain access the same address spaces. Device
 * 00:00.0 accesses address spaces X and Y, each corresponding to an mm_struct.
 * Devices 00:01.* only access address space Y.
 *
 * To obtain the above configuration, users would for instance issue the
 * following calls:
 *
 *     iommu_sva_bind_device(dev 00:00.0, mm X, ...) -> bond 1
 *     iommu_sva_bind_device(dev 00:00.0, mm Y, ...) -> bond 2
 *     iommu_sva_bind_device(dev 00:01.0, mm Y, ...) -> bond 3
 *     iommu_sva_bind_device(dev 00:01.1, mm Y, ...) -> bond 4
 *
 * A single Process Address Space ID (PASID) is allocated for each mm. It is a
 * choice made for the Linux SVA implementation, not a hardware restriction. In
 * the example, devices use PASID 1 to read/write into address space X and PASID
 * 2 to read/write into address space Y. Calling iommu_sva_get_pasid() on bond 1
 * returns 1, and calling it on bonds 2-4 returns 2.
 *
 * Hardware tables describing this configuration in the IOMMU would typically
 * look like this:
 *
 *                                PASID tables
 *                                 of domain A
 *                              .->+--------+
 *                             / 0 |        |-------> io_pgtable
 *                            /    +--------+
 *            Device tables  /   1 |        |-------> pgd X
 *              +--------+  /      +--------+
 *      00:00.0 |      A |-'     2 |        |--.
 *              +--------+         +--------+   \
 *              :        :       3 |        |    \
 *              +--------+         +--------+     --> pgd Y
 *      00:01.0 |      B |--.                    /
 *              +--------+   \                  |
 *      00:01.1 |      B |----+   PASID tables  |
 *              +--------+     \   of domain B  |
 *                              '->+--------+   |
 *                               0 |        |-- | --> io_pgtable
 *                                 +--------+   |
 *                               1 |        |   |
 *                                 +--------+   |
 *                               2 |        |---'
 *                                 +--------+
 *                               3 |        |
 *                                 +--------+
 *
 * With this model, a single call binds all devices in a given domain to an
 * address space. Other devices in the domain will get the same bond implicitly.
 * However, users must issue one bind() for each device, because IOMMUs may
 * implement SVA differently. Furthermore, mandating one bind() per device
 * allows the driver to perform sanity-checks on device capabilities.
 *
 * In some IOMMUs, one entry of the PASID table (typically the first one) can
 * hold non-PASID translations. In this case PASID 0 is reserved and the first
 * entry points to the io_pgtable pointer. In other IOMMUs PASID 0 is available
 * to the allocator.
 */

struct io_mm {
	struct hlist_head		devices;
	struct mm_struct		*mm;
	struct mmu_notifier		notifier;

	/* Late initialization */
	const struct io_mm_ops		*ops;
	void				*ctx;
	int				pasid;
};

#define mn_to_io_mm(mmu_notifier) \
	container_of(mmu_notifier, struct io_mm, notifier)

struct iommu_bond {
	struct iommu_sva		sva;
	struct io_mm			*io_mm;

	struct hlist_node		mm_node;
	void				*drvdata;
	struct rcu_head			rcu_head;
	refcount_t			refs;
	bool				cleared;
};

#define to_iommu_bond(handle) container_of(handle, struct iommu_bond, sva)

static DECLARE_IOASID_SET(shared_pasid);

/*
 * Serializes modifications of bonds.
 * Lock order: Device SVA mutex; global SVA mutex; IOASID lock
 */
static DEFINE_MUTEX(iommu_sva_lock);

struct io_mm_alloc_params {
	const struct io_mm_ops *ops;
	int min_pasid, max_pasid;
};

static struct mmu_notifier *io_mm_alloc(struct mm_struct *mm, void *privdata)
{
	int ret;
	struct io_mm *io_mm;
	struct io_mm_alloc_params *params = privdata;

	io_mm = kzalloc(sizeof(*io_mm), GFP_KERNEL);
	if (!io_mm)
		return ERR_PTR(-ENOMEM);

	io_mm->mm = mm;
	io_mm->ops = params->ops;
	INIT_HLIST_HEAD(&io_mm->devices);

	io_mm->pasid = ioasid_alloc(&shared_pasid, params->min_pasid,
				    params->max_pasid, io_mm->mm);
	if (io_mm->pasid == INVALID_IOASID) {
		ret = -ENOSPC;
		goto err_free_io_mm;
	}

	io_mm->ctx = params->ops->alloc(mm);
	if (IS_ERR(io_mm->ctx)) {
		ret = PTR_ERR(io_mm->ctx);
		goto err_free_pasid;
	}
	trace_io_mm_alloc(io_mm->pasid);
	atomic_inc(&sva_nr_mms);
	return &io_mm->notifier;

err_free_pasid:
	ioasid_free(io_mm->pasid);
err_free_io_mm:
	kfree(io_mm);
	return ERR_PTR(ret);
}

static void io_mm_free(struct mmu_notifier *mn)
{
	struct io_mm *io_mm = mn_to_io_mm(mn);

	WARN_ON(!hlist_empty(&io_mm->devices));

	atomic_dec(&sva_nr_mms);
	trace_io_mm_free(io_mm->pasid);
	io_mm->ops->free(io_mm->ctx);
	ioasid_free(io_mm->pasid);
	kfree(io_mm);
}

static void io_mm_invalidate_range(struct mmu_notifier *mn,
				   struct mm_struct *mm, unsigned long start,
				   unsigned long end)
{
	struct iommu_bond *bond;
	struct io_mm *io_mm = mn_to_io_mm(mn);

	rcu_read_lock();
	hlist_for_each_entry_rcu(bond, &io_mm->devices, mm_node)
		io_mm->ops->invalidate(bond->sva.dev, io_mm->pasid, io_mm->ctx,
				       start, end - start);
	if (!hlist_empty(&io_mm->devices))
		trace_io_mm_invalidate(io_mm->pasid, start, end);
	rcu_read_unlock();
}

/*
 * io_mm_release - release MMU notifier
 *
 * Called when the mm exits. To avoid spending too much time in here, we only
 * clear page table pointers and invalidate IOTLBs here, but we don't stop DMA
 * or free anything here.
 */
static void io_mm_release(struct mmu_notifier *mn, struct mm_struct *mm)
{
	struct iommu_bond *bond;
	struct io_mm *io_mm = mn_to_io_mm(mn);

	mutex_lock(&iommu_sva_lock);
	hlist_for_each_entry(bond, &io_mm->devices, mm_node) {
		/* The release notifier could fire multiple times. */
		if (bond->cleared)
			continue;

		io_mm->ops->clear(bond->sva.dev, io_mm->pasid, io_mm->ctx);
		trace_io_mm_exit(io_mm->pasid, bond->sva.dev);
		bond->cleared = true;
	}
	mutex_unlock(&iommu_sva_lock);
	trace_io_mm_release_done(1);
}

static struct mmu_notifier_ops iommu_mmu_notifier_ops = {
	.alloc_notifier		= io_mm_alloc,
	.free_notifier		= io_mm_free,
	.invalidate_range	= io_mm_invalidate_range,
	.release		= io_mm_release,
};

/*
 * io_mm_get - Allocate an io_mm or get the existing one for the given mm
 * @mm: the mm
 * @ops: callbacks for the IOMMU driver
 * @min_pasid: minimum PASID value (inclusive)
 * @max_pasid: maximum PASID value (inclusive)
 *
 * Returns a valid io_mm or an error pointer.
 */
static struct io_mm *io_mm_get(struct mm_struct *mm,
			       const struct io_mm_ops *ops,
			       int min_pasid, int max_pasid)
{
	struct io_mm *io_mm;
	struct mmu_notifier *mn;
	struct io_mm_alloc_params params = {
		.ops		= ops,
		.min_pasid	= min_pasid,
		.max_pasid	= max_pasid,
	};

	/*
	 * A single notifier can exist for this (ops, mm) pair. Allocate it if
	 * necessary.
	 */
	mn = mmu_notifier_get(&iommu_mmu_notifier_ops, mm, &params);
	if (IS_ERR(mn))
		return ERR_CAST(mn);

	io_mm = mn_to_io_mm(mn);
	if (WARN_ON(io_mm->ops != ops)) {
		mmu_notifier_put(mn);
		return ERR_PTR(-EINVAL);
	}

	return io_mm;
}

static void io_mm_put(struct io_mm *io_mm)
{
	mmu_notifier_put(&io_mm->notifier);
}

static struct iommu_sva *
io_mm_attach(struct device *dev, struct io_mm *io_mm, void *drvdata)
{
	int ret;
	bool attach_domain = true;
	struct iommu_bond *bond, *tmp;
	struct iommu_domain *domain, *other;
	struct iommu_sva_param *param = dev->iommu->sva_param;

	domain = iommu_get_domain_for_dev(dev);

	/* Is it already bound to the device or domain? */
	hlist_for_each_entry(tmp, &io_mm->devices, mm_node) {
		if (tmp->sva.dev == dev) {
			if (WARN_ON(tmp->drvdata != drvdata))
				return ERR_PTR(-EINVAL);

			/*
			 * Hold a single io_mm reference per bond. Note that we
			 * can't return an error after this, otherwise the
			 * caller would drop an additional reference to the
			 * io_mm.
			 */
			refcount_inc(&tmp->refs);
			io_mm_put(io_mm);
			trace_io_mm_attach_get(io_mm->pasid, dev);
			return &tmp->sva;
		}

		if (!attach_domain)
			continue;

		other = iommu_get_domain_for_dev(tmp->sva.dev);
		if (domain == other)
			attach_domain = false;
	}

	bond = kzalloc(sizeof(*bond), GFP_KERNEL);
	if (!bond)
		return ERR_PTR(-ENOMEM);

	bond->sva.dev	= dev;
	bond->drvdata	= drvdata;
	bond->io_mm	= io_mm;
	refcount_set(&bond->refs, 1);

	hlist_add_head_rcu(&bond->mm_node, &io_mm->devices);

	trace_io_mm_attach_alloc(io_mm->pasid, dev);
	ret = io_mm->ops->attach(bond->sva.dev, io_mm->pasid, io_mm->ctx,
				 attach_domain);
	if (ret)
		goto err_remove;

	param->nr_bonds++;
	return &bond->sva;

err_remove:
	/*
	 * At this point concurrent threads may have started to access the
	 * io_mm->devices list in order to invalidate address ranges, which
	 * requires to free the bond via kfree_rcu()
	 */
	hlist_del_init_rcu(&bond->mm_node);
	kfree_rcu(bond, rcu_head);
	return ERR_PTR(ret);
}

static void io_mm_detach(struct iommu_bond *bond)
{
	struct iommu_bond *tmp;
	bool detach_domain = true;
	struct io_mm *io_mm = bond->io_mm;
	struct device *dev = bond->sva.dev;
	struct iommu_domain *domain, *other;
	struct iommu_sva_param *param = dev->iommu->sva_param;

	if (!refcount_dec_and_test(&bond->refs)) {
		trace_io_mm_detach_put(io_mm->pasid, bond->sva.dev);
		return;
	}

	param->nr_bonds--;

	domain = iommu_get_domain_for_dev(bond->sva.dev);

	/* Are other devices in the same domain still attached to this mm? */
	hlist_for_each_entry(tmp, &io_mm->devices, mm_node) {
		if (tmp == bond)
			continue;
		other = iommu_get_domain_for_dev(tmp->sva.dev);
		if (domain == other) {
			detach_domain = false;
			break;
		}
	}

	trace_io_mm_detach_free(io_mm->pasid, bond->sva.dev);
	io_mm->ops->detach(bond->sva.dev, io_mm->pasid, io_mm->ctx,
			   detach_domain, bond->cleared);

	hlist_del_init_rcu(&bond->mm_node);
	kfree_rcu(bond, rcu_head);
	io_mm_put(io_mm);
}

struct iommu_sva *
iommu_sva_bind_generic(struct device *dev, struct mm_struct *mm,
		       const struct io_mm_ops *ops, void *drvdata)
{
	struct io_mm *io_mm;
	struct iommu_sva *handle;
	struct dev_iommu *param = dev->iommu;

	if (!param)
		return ERR_PTR(-ENODEV);

	mutex_lock(&param->sva_lock);
	mutex_lock(&iommu_sva_lock);
	if (!param->sva_param) {
		handle = ERR_PTR(-ENODEV);
		goto out_unlock;
	}

	io_mm = io_mm_get(mm, ops, param->sva_param->min_pasid,
			  param->sva_param->max_pasid);
	if (IS_ERR(io_mm)) {
		handle = ERR_CAST(io_mm);
		goto out_unlock;
	}

	handle = io_mm_attach(dev, io_mm, drvdata);
	if (IS_ERR(handle))
		io_mm_put(io_mm);

out_unlock:
	mutex_unlock(&iommu_sva_lock);
	mutex_unlock(&param->sva_lock);
	return handle;
}
EXPORT_SYMBOL_GPL(iommu_sva_bind_generic);

void iommu_sva_unbind_generic(struct iommu_sva *handle)
{
	struct iommu_bond *bond = to_iommu_bond(handle);
	struct dev_iommu *param = handle->dev->iommu;

	if (WARN_ON(!param))
		return;

	/*
	 * Caller stopped the device from issuing PASIDs, now make sure they are
	 * out of the fault queue.
	 */
	iopf_queue_flush_dev(handle->dev, bond->io_mm->pasid);

	mutex_lock(&param->sva_lock);
	mutex_lock(&iommu_sva_lock);
	io_mm_detach(bond);
	mutex_unlock(&iommu_sva_lock);
	mutex_unlock(&param->sva_lock);
}
EXPORT_SYMBOL_GPL(iommu_sva_unbind_generic);

/**
 * iommu_sva_enable() - Enable Shared Virtual Addressing for a device
 * @dev: the device
 * @sva_param: the parameters.
 *
 * Called by an IOMMU driver to setup the SVA parameters
 * @sva_param is duplicated and can be freed when this function returns.
 *
 * Return 0 if initialization succeeded, or an error.
 */
int iommu_sva_enable(struct device *dev, struct iommu_sva_param *sva_param)
{
	int ret;
	struct iommu_sva_param *new_param;
	struct dev_iommu *param = dev->iommu;

	if (!param)
		return -ENODEV;

	new_param = kmemdup(sva_param, sizeof(*new_param), GFP_KERNEL);
	if (!new_param)
		return -ENOMEM;

	mutex_lock(&param->sva_lock);
	if (param->sva_param) {
		ret = -EEXIST;
		goto err_unlock;
	}

	ret = iommu_register_device_fault_handler(dev, iommu_queue_iopf, dev);
	if (ret)
		goto err_unlock;

	dev->iommu->sva_param = new_param;
	mutex_unlock(&param->sva_lock);
	return 0;

err_unlock:
	mutex_unlock(&param->sva_lock);
	kfree(new_param);
	return ret;
}
EXPORT_SYMBOL_GPL(iommu_sva_enable);

/**
 * iommu_sva_disable() - Disable Shared Virtual Addressing for a device
 * @dev: the device
 *
 * IOMMU drivers call this to disable SVA.
 */
int iommu_sva_disable(struct device *dev)
{
	int ret = 0;
	struct dev_iommu *param = dev->iommu;

	if (!param)
		return -EINVAL;

	mutex_lock(&param->sva_lock);
	if (!param->sva_param) {
		ret = -ENODEV;
		goto out_unlock;
	}

	/* Require that all contexts are unbound */
	if (param->sva_param->nr_bonds) {
		ret = -EBUSY;
		goto out_unlock;
	}

	iommu_unregister_device_fault_handler(dev);
	kfree(param->sva_param);
	param->sva_param = NULL;
out_unlock:
	mutex_unlock(&param->sva_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(iommu_sva_disable);

bool iommu_sva_enabled(struct device *dev)
{
	bool enabled;
	struct dev_iommu *param = dev->iommu;

	if (!param)
		return false;

	mutex_lock(&param->sva_lock);
	enabled = !!param->sva_param;
	mutex_unlock(&param->sva_lock);
	return enabled;
}
EXPORT_SYMBOL_GPL(iommu_sva_enabled);

int iommu_sva_get_pasid_generic(struct iommu_sva *handle)
{
	struct iommu_bond *bond = to_iommu_bond(handle);

	return bond->io_mm->pasid;
}
EXPORT_SYMBOL_GPL(iommu_sva_get_pasid_generic);

/* ioasid wants a void * argument */
static bool __mmget_not_zero(void *mm)
{
	return mmget_not_zero(mm);
}

/**
 * iommu_sva_find() - Find mm associated to the given PASID
 * @pasid: Process Address Space ID assigned to the mm
 *
 * Returns the mm corresponding to this PASID, or an error if not found. A
 * reference to the mm is taken, and must be released with mmput().
 */
struct mm_struct *iommu_sva_find(int pasid)
{
	return ioasid_find(&shared_pasid, pasid, __mmget_not_zero);
}
EXPORT_SYMBOL_GPL(iommu_sva_find);

static int __init make_debugfs(void)
{
	debugfs_create_atomic_t("sva_io_mms", 0444, NULL, &sva_nr_mms);
	return 0;
}

module_init(make_debugfs);

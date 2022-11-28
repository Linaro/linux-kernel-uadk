// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2021-2022, NVIDIA CORPORATION & AFFILIATES
 */
#include <linux/iommufd.h>
#include <linux/slab.h>
#include <linux/iommu.h>
#include <linux/irqdomain.h>
#include <uapi/linux/iommufd.h>

#include "io_pagetable.h"
#include "iommufd_private.h"

struct iommufd_device *
iommufd_device_get_by_id(struct iommufd_ctx *ictx, u32 dev_id)
{
	struct iommufd_object *dev_obj;

	dev_obj = iommufd_get_object(ictx, dev_id, IOMMUFD_OBJ_DEVICE);
	if (IS_ERR(dev_obj))
		return ERR_PTR(-EINVAL);

	return container_of(dev_obj, struct iommufd_device, obj);
}

void iommufd_device_destroy(struct iommufd_object *obj)
{
	struct iommufd_device *idev =
		container_of(obj, struct iommufd_device, obj);

	iommu_device_release_dma_owner(idev->dev);
	iommu_group_put(idev->group);
	iommufd_ctx_put(idev->ictx);
}

/**
 * iommufd_device_bind - Bind a physical device to an iommu fd
 * @ictx: iommufd file descriptor
 * @dev: Pointer to a physical PCI device struct
 * @id: Output ID number to return to userspace for this device
 *
 * A successful bind establishes an ownership over the device and returns
 * struct iommufd_device pointer, otherwise returns error pointer.
 *
 * A driver using this API must set driver_managed_dma and must not touch
 * the device until this routine succeeds and establishes ownership.
 *
 * Binding a PCI device places the entire RID under iommufd control.
 *
 * The caller must undo this with iommufd_device_unbind()
 */
struct iommufd_device *iommufd_device_bind(struct iommufd_ctx *ictx,
					   struct device *dev, u32 *id)
{
	struct iommufd_device *idev;
	struct iommu_group *group;
	int rc;

	/*
	 * iommufd always sets IOMMU_CACHE because we offer no way for userspace
	 * to restore cache coherency.
	 */
	if (!device_iommu_capable(dev, IOMMU_CAP_CACHE_COHERENCY))
		return ERR_PTR(-EINVAL);

	group = iommu_group_get(dev);
	if (!group)
		return ERR_PTR(-ENODEV);

	rc = iommu_device_claim_dma_owner(dev, ictx);
	if (rc)
		goto out_group_put;

	idev = iommufd_object_alloc(ictx, idev, IOMMUFD_OBJ_DEVICE);
	if (IS_ERR(idev)) {
		rc = PTR_ERR(idev);
		goto out_release_owner;
	}
	idev->ictx = ictx;
	iommufd_ctx_get(ictx);
	idev->dev = dev;
	idev->enforce_cache_coherency =
		device_iommu_capable(dev, IOMMU_CAP_ENFORCE_CACHE_COHERENCY);
	/* The calling driver is a user until iommufd_device_unbind() */
	refcount_inc(&idev->obj.users);
	/* group refcount moves into iommufd_device */
	idev->group = group;

	/*
	 * If the caller fails after this success it must call
	 * iommufd_unbind_device() which is safe since we hold this refcount.
	 * This also means the device is a leaf in the graph and no other object
	 * can take a reference on it.
	 */
	iommufd_object_finalize(ictx, &idev->obj);
	*id = idev->obj.id;
	return idev;

out_release_owner:
	iommu_device_release_dma_owner(dev);
out_group_put:
	iommu_group_put(group);
	return ERR_PTR(rc);
}
EXPORT_SYMBOL_NS_GPL(iommufd_device_bind, IOMMUFD);

/**
 * iommufd_device_unbind - Undo iommufd_device_bind()
 * @idev: Device returned by iommufd_device_bind()
 *
 * Release the device from iommufd control. The DMA ownership will return back
 * to unowned with DMA controlled by the DMA API. This invalidates the
 * iommufd_device pointer, other APIs that consume it must not be called
 * concurrently.
 */
void iommufd_device_unbind(struct iommufd_device *idev)
{
	bool was_destroyed;

	was_destroyed = iommufd_object_destroy_user(idev->ictx, &idev->obj);
	WARN_ON(!was_destroyed);
}
EXPORT_SYMBOL_NS_GPL(iommufd_device_unbind, IOMMUFD);

int iommufd_device_get_info(struct iommufd_ucmd *ucmd)
{
	struct iommu_device_info *cmd = ucmd->cmd;
	struct iommufd_object *obj;
	struct iommufd_device *idev;
	struct iommu_hw_info hw_info;
	void *data = NULL;
	int rc;

	if (cmd->flags || cmd->__reserved)
		return -EOPNOTSUPP;

	obj = iommufd_get_object(ucmd->ictx, cmd->dev_id, IOMMUFD_OBJ_DEVICE);
	if (IS_ERR(obj))
		return PTR_ERR(obj);

	idev = container_of(obj, struct iommufd_device, obj);

	if (cmd->out_data_len) {
		data = kzalloc(cmd->out_data_len, GFP_KERNEL);
		hw_info.data_len = cmd->out_data_len;
		hw_info.data = data;
	}

	rc = iommu_get_hw_info(idev->dev, &hw_info);
	if (rc < 0)
		goto out_free_data;

	cmd->out_device_type = hw_info.device_type;

	if (copy_to_user((void __user *)cmd->out_data_ptr,
			 data, (unsigned long)cmd->out_data_len)) {
		rc = -EFAULT;
		goto out_free_data;
	}

	rc = iommufd_ucmd_respond(ucmd, sizeof(*cmd));
	if (rc)
		goto out_put;

out_free_data:
	kfree(data);
out_put:
	iommufd_put_object(obj);
	return rc;
}

static int iommufd_device_setup_msi(struct iommufd_device *idev,
				    struct iommufd_hw_pagetable *hwpt,
				    phys_addr_t sw_msi_start,
				    unsigned int flags)
{
	int rc;

	/*
	 * IOMMU_CAP_INTR_REMAP means that the platform is isolating MSI, and it
	 * creates the MSI window by default in the iommu domain. Nothing
	 * further to do.
	 */
	if (device_iommu_capable(idev->dev, IOMMU_CAP_INTR_REMAP))
		return 0;

	/*
	 * On ARM systems that set the global IRQ_DOMAIN_FLAG_MSI_REMAP every
	 * allocated iommu_domain will block interrupts by default and this
	 * special flow is needed to turn them back on. iommu_dma_prepare_msi()
	 * will install pages into our domain after request_irq() to make this
	 * work.
	 *
	 * FIXME: This is conceptually broken for iommufd since we want to allow
	 * userspace to change the domains, eg switch from an identity IOAS to a
	 * DMA IOAS. There is currently no way to create a MSI window that
	 * matches what the IRQ layer actually expects in a newly created
	 * domain.
	 */
	if (irq_domain_check_msi_remap()) {
		if (WARN_ON(!sw_msi_start))
			return -EPERM;
		/*
		 * iommu_get_msi_cookie() can only be called once per domain,
		 * it returns -EBUSY on later calls.
		 */
		if (hwpt->msi_cookie)
			return 0;
		rc = iommu_get_msi_cookie(hwpt->domain, sw_msi_start);
		if (rc)
			return rc;
		hwpt->msi_cookie = true;
		return 0;
	}

	/*
	 * Otherwise the platform has a MSI window that is not isolated. For
	 * historical compat with VFIO allow a module parameter to ignore the
	 * insecurity.
	 */
	if (!(flags & IOMMUFD_ATTACH_FLAGS_ALLOW_UNSAFE_INTERRUPT))
		return -EPERM;

	dev_warn(
		idev->dev,
		"Device interrupts cannot be isolated by the IOMMU, this platform in insecure. Use an \"allow_unsafe_interrupts\" module parameter to override\n");
	return 0;
}

static bool iommufd_hw_pagetable_has_group(struct iommufd_hw_pagetable *hwpt,
					   struct iommu_group *group)
{
	struct iommufd_device *cur_dev;

	list_for_each_entry(cur_dev, &hwpt->devices, devices_item)
		if (cur_dev->group == group)
			return true;
	return false;
}

static int iommufd_device_attach_ioas(struct iommufd_device *idev,
				      struct iommufd_hw_pagetable *hwpt,
				      unsigned int flags)
{
	phys_addr_t sw_msi_start = 0;
	struct io_pagetable *iopt;
	int rc;

	/* Always use the parent hwpt for IOAS */
	if (hwpt->parent)
		hwpt = hwpt->parent;
	iopt = &hwpt->ioas->iopt;

	rc = iopt_table_enforce_group_resv_regions(iopt, idev->dev,
						   idev->group, &sw_msi_start);
	if (rc)
		return rc;

	rc = iommufd_device_setup_msi(idev, hwpt, sw_msi_start, flags);
	if (rc)
		goto out_iova;

	if (!iommufd_hw_pagetable_has_group(hwpt, idev->group)) {
		if (refcount_read(hwpt->devices_users) == 1) {
			rc = iopt_table_add_domain(iopt, hwpt->domain);
			if (rc)
				goto out_iova;
			list_add_tail(&hwpt->hwpt_item, &hwpt->ioas->hwpt_list);
		}
	}
	return 0;
out_iova:
	iopt_remove_reserved_iova(iopt, idev->group);
	return rc;
}

static void iommufd_device_detach_ioas(struct iommufd_device *idev,
				       struct iommufd_hw_pagetable *hwpt)
{
	if (hwpt->parent)
		hwpt = hwpt->parent;

	if (!iommufd_hw_pagetable_has_group(hwpt, idev->group)) {
		if (refcount_read(hwpt->devices_users) == 1) {
			iopt_table_remove_domain(&hwpt->ioas->iopt,
						 hwpt->domain);
			list_del(&hwpt->hwpt_item);
		}
	}
	iopt_remove_reserved_iova(&hwpt->ioas->iopt, idev->dev);
}

static int iommufd_device_do_attach(struct iommufd_device *idev,
				    struct iommufd_hw_pagetable *hwpt,
				    unsigned int flags)
{
	int rc;

	lockdep_assert_held(&hwpt->ioas->mutex);

	mutex_lock(hwpt->devices_lock);

	/*
	 * Try to upgrade the domain we have, it is an iommu driver bug to
	 * report IOMMU_CAP_ENFORCE_CACHE_COHERENCY but fail
	 * enforce_cache_coherency when there are no devices attached to the
	 * domain.
	 */
	if (idev->enforce_cache_coherency && !hwpt->enforce_cache_coherency) {
		if (hwpt->domain->ops->enforce_cache_coherency)
			hwpt->enforce_cache_coherency =
				hwpt->domain->ops->enforce_cache_coherency(
					hwpt->domain);
		if (!hwpt->enforce_cache_coherency) {
			WARN_ON(list_empty(&hwpt->devices));
			rc = -EINVAL;
			goto out_unlock;
		}
	}

	/*
	 * FIXME: Hack around missing a device-centric iommu api, only attach to
	 * the group once for the first device that is in the group.
	 */
	if (!iommufd_hw_pagetable_has_group(hwpt, idev->group)) {
		rc = iommu_attach_group(hwpt->domain, idev->group);
		if (rc)
			goto out_unlock;
	}

	rc = iommufd_device_attach_ioas(idev, hwpt, flags);
	if (rc)
		goto out_detach;

	idev->hwpt = hwpt;
	refcount_inc(&hwpt->obj.users);
	refcount_inc(hwpt->devices_users);
	list_add(&idev->devices_item, &hwpt->devices);
	mutex_unlock(hwpt->devices_lock);
	return 0;

out_detach:
	iommu_detach_group(hwpt->domain, idev->group);
out_unlock:
	mutex_unlock(hwpt->devices_lock);
	return rc;
}

/*
 * When automatically managing the domains we search for a compatible domain in
 * the iopt and if one is found use it, otherwise create a new domain.
 * Automatic domain selection will never pick a manually created domain.
 */
static int iommufd_device_auto_get_domain(struct iommufd_device *idev,
					  struct iommufd_ioas *ioas,
					  unsigned int flags)
{
	struct iommufd_hw_pagetable *hwpt;
	int rc;

	/*
	 * There is no differentiation when domains are allocated, so any domain
	 * that is willing to attach to the device is interchangeable with any
	 * other.
	 */
	list_for_each_entry(hwpt, &ioas->hwpt_list, hwpt_item) {
		if (!hwpt->auto_domain)
			continue;

		rc = iommufd_device_do_attach(idev, hwpt, flags);

		/*
		 * -EINVAL means the domain is incompatible with the device.
		 * Other error codes should propagate to userspace as failure.
		 * Success means the domain is attached.
		 */
		if (rc == -EINVAL)
			continue;
		return rc;
	}

	hwpt = iommufd_hw_pagetable_alloc(idev->ictx, ioas, idev->dev);
	if (IS_ERR(hwpt))
		return PTR_ERR(hwpt);
	hwpt->auto_domain = true;

	rc = iommufd_device_do_attach(idev, hwpt, flags);
	if (rc)
		goto out_abort;

	iommufd_object_finalize(idev->ictx, &hwpt->obj);
	return 0;

out_abort:
	iommufd_object_abort_and_destroy(idev->ictx, &hwpt->obj);
	return rc;
}

/**
 * iommufd_device_attach - Connect a device to an iommu_domain
 * @idev: device to attach
 * @pt_id: Input a IOMMUFD_OBJ_IOAS, or IOMMUFD_OBJ_HW_PAGETABLE
 *         Output the IOMMUFD_OBJ_HW_PAGETABLE ID
 * @flags: Optional flags
 *
 * This connects the device to an iommu_domain, either automatically or manually
 * selected. Once this completes the device could do DMA.
 *
 * The caller should return the resulting pt_id back to userspace.
 * This function is undone by calling iommufd_device_detach().
 */
int iommufd_device_attach(struct iommufd_device *idev, u32 *pt_id,
			  unsigned int flags)
{
	struct iommufd_object *pt_obj;
	int rc;

	pt_obj = iommufd_get_object(idev->ictx, *pt_id, IOMMUFD_OBJ_ANY);
	if (IS_ERR(pt_obj))
		return PTR_ERR(pt_obj);

	switch (pt_obj->type) {
	case IOMMUFD_OBJ_HW_PAGETABLE: {
		struct iommufd_hw_pagetable *hwpt =
			container_of(pt_obj, struct iommufd_hw_pagetable, obj);

		mutex_lock(&hwpt->ioas->mutex);
		rc = iommufd_device_do_attach(idev, hwpt, flags);
		mutex_unlock(&hwpt->ioas->mutex);
		if (rc)
			goto out_put_pt_obj;
		break;
	}
	case IOMMUFD_OBJ_IOAS: {
		struct iommufd_ioas *ioas =
			container_of(pt_obj, struct iommufd_ioas, obj);

		mutex_lock(&ioas->mutex);
		rc = iommufd_device_auto_get_domain(idev, ioas, flags);
		mutex_unlock(&ioas->mutex);
		if (rc)
			goto out_put_pt_obj;
		break;
	}
	default:
		rc = -EINVAL;
		goto out_put_pt_obj;
	}

	refcount_inc(&idev->obj.users);
	*pt_id = idev->hwpt->obj.id;
	rc = 0;

out_put_pt_obj:
	iommufd_put_object(pt_obj);
	return rc;
}
EXPORT_SYMBOL_NS_GPL(iommufd_device_attach, IOMMUFD);

/**
 * iommufd_device_detach - Disconnect a device to an iommu_domain
 * @idev: device to detach
 *
 * Undo iommufd_device_attach(). This disconnects the idev from the previously
 * attached pt_id. The device returns back to a blocked DMA translation.
 */
void iommufd_device_detach(struct iommufd_device *idev)
{
	struct iommufd_hw_pagetable *hwpt = idev->hwpt;

	mutex_lock(&hwpt->ioas->mutex);
	mutex_lock(hwpt->devices_lock);
	refcount_dec(hwpt->devices_users);
	list_del(&idev->devices_item);
	iommufd_device_detach_ioas(idev, hwpt);
	if (!iommufd_hw_pagetable_has_group(hwpt, idev->group))
		iommu_detach_group(hwpt->domain, idev->group);
	mutex_unlock(hwpt->devices_lock);
	mutex_unlock(&hwpt->ioas->mutex);

	if (hwpt->auto_domain)
		iommufd_object_destroy_user(idev->ictx, &hwpt->obj);
	else
		refcount_dec(&hwpt->obj.users);

	idev->hwpt = NULL;

	refcount_dec(&idev->obj.users);
}
EXPORT_SYMBOL_NS_GPL(iommufd_device_detach, IOMMUFD);

void iommufd_access_destroy_object(struct iommufd_object *obj)
{
	struct iommufd_access *access =
		container_of(obj, struct iommufd_access, obj);

	iopt_remove_access(&access->ioas->iopt, access);
	iommufd_ctx_put(access->ictx);
	refcount_dec(&access->ioas->obj.users);
}

/**
 * iommufd_access_create - Create an iommufd_access
 * @ictx: iommufd file descriptor
 * @ioas_id: ID for a IOMMUFD_OBJ_IOAS
 * @ops: Driver's ops to associate with the access
 * @data: Opaque data to pass into ops functions
 *
 * An iommufd_access allows a driver to read/write to the IOAS without using
 * DMA. The underlying CPU memory can be accessed using the
 * iommufd_access_pin_pages() or iommufd_access_rw() functions.
 *
 * The provided ops are required to use iommufd_access_pin_pages().
 */
struct iommufd_access *
iommufd_access_create(struct iommufd_ctx *ictx, u32 ioas_id,
		      const struct iommufd_access_ops *ops, void *data)
{
	struct iommufd_access *access;
	struct iommufd_object *obj;
	int rc;

	/*
	 * There is no uAPI for the access object, but to keep things symmetric
	 * use the object infrastructure anyhow.
	 */
	access = iommufd_object_alloc(ictx, access, IOMMUFD_OBJ_ACCESS);
	if (IS_ERR(access))
		return access;

	access->data = data;
	access->ops = ops;

	obj = iommufd_get_object(ictx, ioas_id, IOMMUFD_OBJ_IOAS);
	if (IS_ERR(obj)) {
		rc = PTR_ERR(obj);
		goto out_abort;
	}
	access->ioas = container_of(obj, struct iommufd_ioas, obj);
	iommufd_ref_to_users(obj);

	if (ops->needs_pin_pages)
		access->iova_alignment = PAGE_SIZE;
	else
		access->iova_alignment = 1;
	rc = iopt_add_access(&access->ioas->iopt, access);
	if (rc)
		goto out_put_ioas;

	/* The calling driver is a user until iommufd_access_destroy() */
	refcount_inc(&access->obj.users);
	access->ictx = ictx;
	iommufd_ctx_get(ictx);
	iommufd_object_finalize(ictx, &access->obj);
	return access;
out_put_ioas:
	refcount_dec(&access->ioas->obj.users);
out_abort:
	iommufd_object_abort(ictx, &access->obj);
	return ERR_PTR(rc);
}
EXPORT_SYMBOL_NS_GPL(iommufd_access_create, IOMMUFD);

/**
 * iommufd_access_destroy - Destroy an iommufd_access
 * @access: The access to destroy
 *
 * The caller must stop using the access before destroying it.
 */
void iommufd_access_destroy(struct iommufd_access *access)
{
	bool was_destroyed;

	was_destroyed = iommufd_object_destroy_user(access->ictx, &access->obj);
	WARN_ON(!was_destroyed);
}
EXPORT_SYMBOL_NS_GPL(iommufd_access_destroy, IOMMUFD);

/**
 * iommufd_access_notify_unmap - Notify users of an iopt to stop using it
 * @iopt: iopt to work on
 * @iova: Starting iova in the iopt
 * @length: Number of bytes
 *
 * After this function returns there should be no users attached to the pages
 * linked to this iopt that intersect with iova,length. Anyone that has attached
 * a user through iopt_access_pages() needs to detatch it through
 * iommufd_access_unpin_pages() before this function returns.
 *
 * The unmap callback may not call or wait for a iommufd_access_destroy() to
 * complete. Once iommufd_access_destroy() returns no ops are running and no
 * future ops will be called.
 */
void iommufd_access_notify_unmap(struct io_pagetable *iopt, unsigned long iova,
				 unsigned long length)
{
	struct iommufd_ioas *ioas =
		container_of(iopt, struct iommufd_ioas, iopt);
	struct iommufd_access *access;
	unsigned long index;

	xa_lock(&ioas->iopt.access_list);
	xa_for_each(&ioas->iopt.access_list, index, access) {
		if (!iommufd_lock_obj(&access->obj))
			continue;
		xa_unlock(&ioas->iopt.access_list);

		access->ops->unmap(access->data, iova, length);

		iommufd_put_object(&access->obj);
		xa_lock(&ioas->iopt.access_list);
	}
	xa_unlock(&ioas->iopt.access_list);
}

/**
 * iommufd_access_unpin_pages() - Undo iommufd_access_pin_pages
 * @access: IOAS access to act on
 * @iova: Starting IOVA
 * @length:- Number of bytes to access
 *
 * Return the struct page's. The caller must stop accessing them before calling
 * this. The iova/length must exactly match the one provided to access_pages.
 */
void iommufd_access_unpin_pages(struct iommufd_access *access,
				unsigned long iova, unsigned long length)
{
	struct io_pagetable *iopt = &access->ioas->iopt;
	struct iopt_area_contig_iter iter;
	unsigned long last_iova;
	struct iopt_area *area;

	if (WARN_ON(!length) ||
	    WARN_ON(check_add_overflow(iova, length - 1, &last_iova)))
		return;

	down_read(&iopt->iova_rwsem);
	iopt_for_each_contig_area(&iter, area, iopt, iova, last_iova)
		iopt_area_remove_access(
			area, iopt_area_iova_to_index(area, iter.cur_iova),
			iopt_area_iova_to_index(
				area,
				min(last_iova, iopt_area_last_iova(area))));
	up_read(&iopt->iova_rwsem);
	WARN_ON(!iopt_area_contig_done(&iter));
}
EXPORT_SYMBOL_NS_GPL(iommufd_access_unpin_pages, IOMMUFD);

static bool iopt_area_contig_is_aligned(struct iopt_area_contig_iter *iter)
{
	if (iopt_area_start_byte(iter->area, iter->cur_iova) % PAGE_SIZE)
		return false;

	if (!iopt_area_contig_done(iter) &&
	    (iopt_area_start_byte(iter->area, iopt_area_last_iova(iter->area)) %
	     PAGE_SIZE) != (PAGE_SIZE - 1))
		return false;
	return true;
}

static bool check_area_prot(struct iopt_area *area, unsigned int flags)
{
	if (flags & IOMMUFD_ACCESS_RW_WRITE)
		return area->iommu_prot & IOMMU_WRITE;
	return area->iommu_prot & IOMMU_READ;
}

/**
 * iommufd_access_pin_pages() - Return a list of pages under the iova
 * @access: IOAS access to act on
 * @iova: Starting IOVA
 * @length: Number of bytes to access
 * @out_pages: Output page list
 * @flags: IOPMMUFD_ACCESS_RW_* flags
 *
 * Reads @length bytes starting at iova and returns the struct page * pointers.
 * These can be kmap'd by the caller for CPU access.
 *
 * The caller must perform iopt_unaccess_pages() when done to balance this.
 *
 * This API always requires a page aligned iova. This happens naturally if the
 * ioas alignment is >= PAGE_SIZE and the iova is PAGE_SIZE aligned. However
 * smaller alignments have corner cases where this API can fail on otherwise
 * aligned iova.
 */
int iommufd_access_pin_pages(struct iommufd_access *access, unsigned long iova,
			     unsigned long length, struct page **out_pages,
			     unsigned int flags)
{
	struct io_pagetable *iopt = &access->ioas->iopt;
	struct iopt_area_contig_iter iter;
	unsigned long last_iova;
	struct iopt_area *area;
	int rc;

	/* Driver's ops don't support pin_pages */
	if (IS_ENABLED(CONFIG_IOMMUFD_TEST) &&
	    WARN_ON(access->iova_alignment != PAGE_SIZE || !access->ops->unmap))
		return -EINVAL;

	if (!length)
		return -EINVAL;
	if (check_add_overflow(iova, length - 1, &last_iova))
		return -EOVERFLOW;

	down_read(&iopt->iova_rwsem);
	iopt_for_each_contig_area(&iter, area, iopt, iova, last_iova) {
		unsigned long last = min(last_iova, iopt_area_last_iova(area));
		unsigned long last_index = iopt_area_iova_to_index(area, last);
		unsigned long index =
			iopt_area_iova_to_index(area, iter.cur_iova);

		if (area->prevent_access ||
		    !iopt_area_contig_is_aligned(&iter)) {
			rc = -EINVAL;
			goto err_remove;
		}

		if (!check_area_prot(area, flags)) {
			rc = -EPERM;
			goto err_remove;
		}

		rc = iopt_area_add_access(area, index, last_index, out_pages,
					  flags);
		if (rc)
			goto err_remove;
		out_pages += last_index - index + 1;
	}
	if (!iopt_area_contig_done(&iter)) {
		rc = -ENOENT;
		goto err_remove;
	}

	up_read(&iopt->iova_rwsem);
	return 0;

err_remove:
	if (iova < iter.cur_iova) {
		last_iova = iter.cur_iova - 1;
		iopt_for_each_contig_area(&iter, area, iopt, iova, last_iova)
			iopt_area_remove_access(
				area,
				iopt_area_iova_to_index(area, iter.cur_iova),
				iopt_area_iova_to_index(
					area, min(last_iova,
						  iopt_area_last_iova(area))));
	}
	up_read(&iopt->iova_rwsem);
	return rc;
}
EXPORT_SYMBOL_NS_GPL(iommufd_access_pin_pages, IOMMUFD);

/**
 * iommufd_access_rw - Read or write data under the iova
 * @access: IOAS access to act on
 * @iova: Starting IOVA
 * @data: Kernel buffer to copy to/from
 * @length: Number of bytes to access
 * @flags: IOMMUFD_ACCESS_RW_* flags
 *
 * Copy kernel to/from data into the range given by IOVA/length. If flags
 * indicates IOMMUFD_ACCESS_RW_KTHREAD then a large copy can be optimized
 * by changing it into copy_to/from_user().
 */
int iommufd_access_rw(struct iommufd_access *access, unsigned long iova,
		      void *data, size_t length, unsigned int flags)
{
	struct io_pagetable *iopt = &access->ioas->iopt;
	struct iopt_area_contig_iter iter;
	struct iopt_area *area;
	unsigned long last_iova;
	int rc;

	if (!length)
		return -EINVAL;
	if (check_add_overflow(iova, length - 1, &last_iova))
		return -EOVERFLOW;

	down_read(&iopt->iova_rwsem);
	iopt_for_each_contig_area(&iter, area, iopt, iova, last_iova) {
		unsigned long last = min(last_iova, iopt_area_last_iova(area));
		unsigned long bytes = (last - iter.cur_iova) + 1;

		if (area->prevent_access) {
			rc = -EINVAL;
			goto err_out;
		}

		if (!check_area_prot(area, flags)) {
			rc = -EPERM;
			goto err_out;
		}

		rc = iopt_pages_rw_access(
			area->pages, iopt_area_start_byte(area, iter.cur_iova),
			data, bytes, flags);
		if (rc)
			goto err_out;
		data += bytes;
	}
	if (!iopt_area_contig_done(&iter))
		rc = -ENOENT;
err_out:
	up_read(&iopt->iova_rwsem);
	return rc;
}
EXPORT_SYMBOL_NS_GPL(iommufd_access_rw, IOMMUFD);

#ifdef CONFIG_IOMMUFD_TEST
/*
 * Creating a real iommufd_device is too hard, bypass creating a iommufd_device
 * and go directly to attaching a domain.
 */
struct iommufd_hw_pagetable *
iommufd_device_selftest_attach(struct iommufd_ctx *ictx,
			       struct iommufd_ioas *ioas,
			       struct device *mock_dev)
{
	struct iommufd_hw_pagetable *hwpt;
	int rc;

	hwpt = iommufd_hw_pagetable_alloc(ictx, ioas, mock_dev);
	if (IS_ERR(hwpt))
		return hwpt;

	rc = iopt_table_add_domain(&hwpt->ioas->iopt, hwpt->domain);
	if (rc)
		goto out_hwpt;

	refcount_inc(&hwpt->obj.users);
	iommufd_object_finalize(ictx, &hwpt->obj);
	return hwpt;

out_hwpt:
	iommufd_object_abort_and_destroy(ictx, &hwpt->obj);
	return ERR_PTR(rc);
}

void iommufd_device_selftest_detach(struct iommufd_ctx *ictx,
				    struct iommufd_hw_pagetable *hwpt)
{
	iopt_table_remove_domain(&hwpt->ioas->iopt, hwpt->domain);
	refcount_dec(&hwpt->obj.users);
}
#endif

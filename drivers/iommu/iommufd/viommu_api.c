// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2024, NVIDIA CORPORATION & AFFILIATES
 */

#include "iommufd_private.h"

struct iommufd_object *iommufd_object_alloc_elm(struct iommufd_ctx *ictx,
						size_t size,
						enum iommufd_object_type type)
{
	struct iommufd_object *obj;
	int rc;

	obj = kzalloc(size, GFP_KERNEL_ACCOUNT);
	if (!obj)
		return ERR_PTR(-ENOMEM);
	obj->type = type;
	/* Starts out bias'd by 1 until it is removed from the xarray */
	refcount_set(&obj->shortterm_users, 1);
	refcount_set(&obj->users, 1);

	/*
	 * Reserve an ID in the xarray but do not publish the pointer yet since
	 * the caller hasn't initialized it yet. Once the pointer is published
	 * in the xarray and visible to other threads we can't reliably destroy
	 * it anymore, so the caller must complete all errorable operations
	 * before calling iommufd_object_finalize().
	 */
	rc = xa_alloc(&ictx->objects, &obj->id, XA_ZERO_ENTRY,
		      xa_limit_31b, GFP_KERNEL_ACCOUNT);
	if (rc)
		goto out_free;
	return obj;
out_free:
	kfree(obj);
	return ERR_PTR(rc);
}
EXPORT_SYMBOL_NS_GPL(iommufd_object_alloc_elm, IOMMUFD);

struct iommufd_viommu *
__iommufd_viommu_alloc(struct iommufd_ctx *ictx, size_t size,
		       const struct iommufd_viommu_ops *ops)
{
	struct iommufd_viommu *viommu;
	struct iommufd_object *obj;

	if (WARN_ON(size < sizeof(*viommu)))
		return ERR_PTR(-EINVAL);
	obj = iommufd_object_alloc_elm(ictx, size, IOMMUFD_OBJ_VIOMMU);
	if (IS_ERR(obj))
		return ERR_CAST(obj);
	viommu = container_of(obj, struct iommufd_viommu, obj);
	if (ops)
		viommu->ops = ops;
	return viommu;
}
EXPORT_SYMBOL_NS_GPL(__iommufd_viommu_alloc, IOMMUFD);

struct iommufd_vdevice *
__iommufd_vdevice_alloc(struct iommufd_ctx *ictx, size_t size)
{
	struct iommufd_object *obj;

	if (WARN_ON(size < sizeof(struct iommufd_vdevice)))
		return ERR_PTR(-EINVAL);
	obj = iommufd_object_alloc_elm(ictx, size, IOMMUFD_OBJ_VDEVICE);
	if (IS_ERR(obj))
		return ERR_CAST(obj);
	return container_of(obj, struct iommufd_vdevice, obj);
}
EXPORT_SYMBOL_NS_GPL(__iommufd_vdevice_alloc, IOMMUFD);

/* Caller should xa_lock(&viommu->vdevs) to protect the return value */
struct device *vdev_to_dev(struct iommufd_vdevice *vdev)
{
	return vdev ? vdev->idev->dev : NULL;
}
EXPORT_SYMBOL_NS_GPL(vdev_to_dev, IOMMUFD);

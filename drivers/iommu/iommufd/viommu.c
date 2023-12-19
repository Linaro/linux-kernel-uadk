// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2024, NVIDIA CORPORATION & AFFILIATES
 */

#include "iommufd_private.h"

void iommufd_viommu_destroy(struct iommufd_object *obj)
{
	struct iommufd_viommu *viommu =
		container_of(obj, struct iommufd_viommu, obj);

	if (viommu->ops && viommu->ops->free)
		viommu->ops->free(viommu);
	refcount_dec(&viommu->hwpt->common.obj.users);
	xa_destroy(&viommu->vdevs);
}

int iommufd_viommu_alloc_ioctl(struct iommufd_ucmd *ucmd)
{
	struct iommu_viommu_alloc *cmd = ucmd->cmd;
	struct iommufd_hwpt_paging *hwpt_paging;
	struct iommufd_viommu *viommu;
	struct iommufd_device *idev;
	const struct iommu_ops *ops;
	int rc;

	if (cmd->flags || cmd->type == IOMMU_VIOMMU_TYPE_DEFAULT)
		return -EOPNOTSUPP;

	idev = iommufd_get_device(ucmd, cmd->dev_id);
	if (IS_ERR(idev))
		return PTR_ERR(idev);

	ops = dev_iommu_ops(idev->dev);
	if (!ops->viommu_alloc) {
		rc = -EOPNOTSUPP;
		goto out_put_idev;
	}

	hwpt_paging = iommufd_get_hwpt_paging(ucmd, cmd->hwpt_id);
	if (IS_ERR(hwpt_paging)) {
		rc = PTR_ERR(hwpt_paging);
		goto out_put_idev;
	}

	if (!hwpt_paging->nest_parent) {
		rc = -EINVAL;
		goto out_put_hwpt;
	}

	viommu = ops->viommu_alloc(idev->dev, hwpt_paging->common.domain,
				   ucmd->ictx, cmd->type);
	if (IS_ERR(viommu)) {
		rc = PTR_ERR(viommu);
		goto out_put_hwpt;
	}

	rc = iommufd_verify_unfinalized_object(ucmd->ictx, &viommu->obj);
	if (rc) {
		kfree(viommu);
		goto out_put_hwpt;
	}

	viommu->type = cmd->type;
	viommu->ictx = ucmd->ictx;
	viommu->hwpt = hwpt_paging;
	/*
	 * It is the most likely case that a physical IOMMU is unpluggable. A
	 * pluggable IOMMU instance (if exists) is responsible for refcounting
	 * on its own.
	 */
	viommu->iommu_dev = __iommu_get_iommu_dev(idev->dev);

	xa_init(&viommu->vdevs);
	refcount_inc(&viommu->hwpt->common.obj.users);

	cmd->out_viommu_id = viommu->obj.id;
	rc = iommufd_ucmd_respond(ucmd, sizeof(*cmd));
	if (rc)
		goto out_abort;
	iommufd_object_finalize(ucmd->ictx, &viommu->obj);
	goto out_put_hwpt;

out_abort:
	iommufd_object_abort_and_destroy(ucmd->ictx, &viommu->obj);
out_put_hwpt:
	iommufd_put_object(ucmd->ictx, &hwpt_paging->common.obj);
out_put_idev:
	iommufd_put_object(ucmd->ictx, &idev->obj);
	return rc;
}

void iommufd_vdevice_abort(struct iommufd_object *obj)
{
	struct iommufd_vdevice *old,
		*vdev = container_of(obj, struct iommufd_vdevice, obj);
	struct iommufd_viommu *viommu = vdev->viommu;
	struct iommufd_device *idev = vdev->idev;

	lockdep_assert_held(&idev->igroup->lock);

	old = xa_cmpxchg(&viommu->vdevs, vdev->id, vdev, NULL, GFP_KERNEL);
	if (old)
		WARN_ON(old != vdev);

	refcount_dec(&viommu->obj.users);
	refcount_dec(&idev->obj.users);
	idev->vdev = NULL;
}

void iommufd_vdevice_destroy(struct iommufd_object *obj)
{
	struct iommufd_vdevice *vdev =
		container_of(obj, struct iommufd_vdevice, obj);

	mutex_lock(&vdev->idev->igroup->lock);
	iommufd_vdevice_abort(obj);
	mutex_unlock(&vdev->idev->igroup->lock);
}

int iommufd_vdevice_alloc_ioctl(struct iommufd_ucmd *ucmd)
{
	struct iommu_vdevice_alloc *cmd = ucmd->cmd;
	struct iommufd_vdevice *vdev, *curr;
	struct iommufd_viommu *viommu;
	struct iommufd_device *idev;
	u64 virt_id = cmd->virt_id;
	int rc = 0;

	if (virt_id > ULONG_MAX)
		return -EINVAL;

	viommu = iommufd_get_viommu(ucmd, cmd->viommu_id);
	if (IS_ERR(viommu))
		return PTR_ERR(viommu);

	idev = iommufd_get_device(ucmd, cmd->dev_id);
	if (IS_ERR(idev)) {
		rc = PTR_ERR(idev);
		goto out_put_viommu;
	}

	mutex_lock(&idev->igroup->lock);
	if (idev->vdev) {
		rc = -EEXIST;
		goto out_unlock_igroup;
	}

	vdev = iommufd_object_alloc(ucmd->ictx, vdev, IOMMUFD_OBJ_VDEVICE);
	if (IS_ERR(vdev)) {
		rc = PTR_ERR(vdev);
		goto out_unlock_igroup;
	}

	rc = iommufd_verify_unfinalized_object(ucmd->ictx, &vdev->obj);
	if (rc) {
		kfree(vdev);
		goto out_unlock_igroup;
	}

	vdev->idev = idev;
	vdev->id = virt_id;
	vdev->viommu = viommu;

	idev->vdev = vdev;
	refcount_inc(&idev->obj.users);
	refcount_inc(&viommu->obj.users);

	curr = xa_cmpxchg(&viommu->vdevs, virt_id, NULL, vdev, GFP_KERNEL);
	if (curr) {
		rc = xa_err(curr) ?: -EBUSY;
		goto out_abort;
	}

	cmd->out_vdevice_id = vdev->obj.id;
	rc = iommufd_ucmd_respond(ucmd, sizeof(*cmd));
	if (rc)
		goto out_abort;
	iommufd_object_finalize(ucmd->ictx, &vdev->obj);
	goto out_unlock_igroup;

out_abort:
	iommufd_object_abort_and_destroy(ucmd->ictx, &vdev->obj);
out_unlock_igroup:
	mutex_unlock(&idev->igroup->lock);
	iommufd_put_object(ucmd->ictx, &idev->obj);
out_put_viommu:
	iommufd_put_object(ucmd->ictx, &viommu->obj);
	return rc;
}

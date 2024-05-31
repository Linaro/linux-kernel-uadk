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
}

int iommufd_viommu_alloc_ioctl(struct iommufd_ucmd *ucmd)
{
	struct iommu_viommu_alloc *cmd = ucmd->cmd;
	struct iommufd_hwpt_paging *hwpt_paging;
	struct iommufd_viommu *viommu;
	struct iommufd_device *idev;
	const struct iommu_ops *ops;
	int rc;

	if (cmd->flags)
		return -EOPNOTSUPP;

	idev = iommufd_get_device(ucmd, cmd->dev_id);
	if (IS_ERR(idev))
		return PTR_ERR(idev);
	ops = dev_iommu_ops(idev->dev);

	hwpt_paging = iommufd_get_hwpt_paging(ucmd, cmd->hwpt_id);
	if (IS_ERR(hwpt_paging)) {
		rc = PTR_ERR(hwpt_paging);
		goto out_put_idev;
	}

	if (!hwpt_paging->nest_parent) {
		rc = -EINVAL;
		goto out_put_hwpt;
	}

	if (cmd->type == IOMMU_VIOMMU_TYPE_DEFAULT) {
		viommu = __iommufd_viommu_alloc(ucmd->ictx, sizeof(*viommu),
						ops->default_viommu_ops);
	} else {
		if (!ops->viommu_alloc) {
			rc = -EOPNOTSUPP;
			goto out_put_hwpt;
		}

		viommu = ops->viommu_alloc(idev->dev->iommu->iommu_dev,
					   hwpt_paging->common.domain,
					   ucmd->ictx, cmd->type);
	}
	if (IS_ERR(viommu)) {
		rc = PTR_ERR(viommu);
		goto out_put_hwpt;
	}

	viommu->type = cmd->type;
	viommu->ictx = ucmd->ictx;
	viommu->hwpt = hwpt_paging;

	/*
	 * A real physical IOMMU instance would unlikely get unplugged, so the
	 * life cycle of this iommu_dev is guaranteed to stay alive, mostly. A
	 * pluggable IOMMU instance (if exists) is responsible for refcounting
	 * on its own.
	 */
	viommu->iommu_dev = idev->dev->iommu->iommu_dev;

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

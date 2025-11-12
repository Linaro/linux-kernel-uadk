// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2025. All rights reserved.
 *
 * Description: ubcore cgroup resource control
 * Author: Xu Zhicong
 * Create: 2023-12-25
 * Note:
 * History: 2023-12-25: create file
 */

#include <ub/urma/ubcore_types.h>
#include <ub/urma/ubcore_uapi.h>
#include "ubcore_log.h"

#ifdef CONFIG_CGROUP_RDMA
static inline bool ubcore_is_use_cg(struct ubcore_device *dev)
{
	return (dev != NULL && dev->transport_type == UBCORE_TRANSPORT_UB &&
		dev->cg_device.dev.name != NULL);
}

void ubcore_cgroup_reg_dev(struct ubcore_device *dev)
{
	if (!dev || strnlen(dev->dev_name, UBCORE_MAX_DEV_NAME) >=
				   UBCORE_MAX_DEV_NAME) {
		ubcore_log_err("Invalid parameter");
		return;
	}

	dev->cg_device.dev.name = dev->dev_name;
	if (!ubcore_is_use_cg(dev))
		return;

	(void)rdmacg_register_device(&dev->cg_device.dev);
}
EXPORT_SYMBOL(ubcore_cgroup_reg_dev);

void ubcore_cgroup_unreg_dev(struct ubcore_device *dev)
{
	if (!ubcore_is_use_cg(dev))
		return;

	rdmacg_unregister_device(&dev->cg_device.dev);
}
EXPORT_SYMBOL(ubcore_cgroup_unreg_dev);

static enum rdmacg_resource_type
ubcore_get_rdma_resource_type(enum ubcore_resource_type type)
{
	switch (type) {
	case UBCORE_RESOURCE_HCA_HANDLE:
		return RDMACG_RESOURCE_HCA_HANDLE;
	case UBCORE_RESOURCE_HCA_OBJECT:
		return RDMACG_RESOURCE_HCA_OBJECT;
	case UBCORE_RESOURCE_HCA_MAX:
	default:
		ubcore_log_err("not support cgroup resource type:%d",
			       (int)type);
	}

	return RDMACG_RESOURCE_MAX;
}

int ubcore_cgroup_try_charge(struct ubcore_cg_object *cg_obj,
			     struct ubcore_device *dev,
			     enum ubcore_resource_type type)
{
	enum rdmacg_resource_type rdma_cg_type;

	if (!cg_obj || !cg_obj->cg)
		return 0;

	if (!ubcore_is_use_cg(dev))
		return 0;

	rdma_cg_type = ubcore_get_rdma_resource_type(type);
	if (rdma_cg_type == RDMACG_RESOURCE_MAX)
		return -EINVAL;

	return rdmacg_try_charge(&cg_obj->cg, &dev->cg_device.dev,
				 rdma_cg_type);
}
EXPORT_SYMBOL(ubcore_cgroup_try_charge);

void ubcore_cgroup_uncharge(struct ubcore_cg_object *cg_obj,
			    struct ubcore_device *dev,
			    enum ubcore_resource_type type)
{
	enum rdmacg_resource_type rdma_cg_type;

	if (!cg_obj || !cg_obj->cg)
		return;

	if (!ubcore_is_use_cg(dev))
		return;

	rdma_cg_type = ubcore_get_rdma_resource_type(type);
	if (rdma_cg_type == RDMACG_RESOURCE_MAX)
		return;

	rdmacg_uncharge(cg_obj->cg, &dev->cg_device.dev, rdma_cg_type);
}
EXPORT_SYMBOL(ubcore_cgroup_uncharge);

#endif // CONFIG_CGROUP_RDMA

// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved.
 *
 */

#include <ub/ubase/ubase_comm_eq.h>

#include "ubase_dev.h"
#include "ubase_eq.h"

static int __ubase_event_register(struct ubase_dev *udev,
				  struct ubase_event_nb *cb)
{
	struct blocking_notifier_head *nh;
	int ret;

	if (cb->drv_type >= UBASE_DRV_MAX) {
		ubase_err(udev, "unsupported drv_type(%u).\n", cb->drv_type);
		return -EINVAL;
	}

	if (cb->event_type >= UBASE_EVENT_TYPE_MAX) {
		ubase_err(udev,
			  "unsupported event type(%u).\n", cb->event_type);
		return -EINVAL;
	}

	nh = udev->irq_table.nh[cb->drv_type];
	ret = blocking_notifier_chain_register(&nh[cb->event_type], &cb->nb);
	if (ret)
		ubase_err(udev,
			  "failed to notifier chain register, type = %u, ret = %d.\n",
			  cb->event_type, ret);

	return ret;
}

int ubase_event_register(struct auxiliary_device *adev,
			 struct ubase_event_nb *cb)
{
	if (!adev || !cb)
		return -EINVAL;

	return __ubase_event_register(__ubase_get_udev_by_adev(adev), cb);
}
EXPORT_SYMBOL(ubase_event_register);

static void __ubase_event_unregister(struct ubase_dev *udev,
				     struct ubase_event_nb *cb)
{
	struct blocking_notifier_head *nh;
	int ret;

	if (cb->drv_type >= UBASE_DRV_MAX) {
		ubase_err(udev, "unsupported drv_type(%u).\n", cb->drv_type);
		return;
	}

	if (cb->event_type >= UBASE_EVENT_TYPE_MAX) {
		ubase_err(udev,
			  "unsupported event type(%u).\n", cb->event_type);
		return;
	}

	nh = udev->irq_table.nh[cb->drv_type];
	ret = blocking_notifier_chain_unregister(&nh[cb->event_type], &cb->nb);
	if (ret)
		ubase_err(udev,
			  "failed to unregister notifier chain, type = %u, ret = %d.\n",
			  cb->event_type, ret);
}

void ubase_event_unregister(struct auxiliary_device *adev,
			    struct ubase_event_nb *cb)
{
	if (!adev || !cb)
		return;

	__ubase_event_unregister(__ubase_get_udev_by_adev(adev), cb);
}
EXPORT_SYMBOL(ubase_event_unregister);

int ubase_comp_register(struct auxiliary_device *adev,
			int (*comp_handler)(struct notifier_block *nb,
					    unsigned long jfcn, void *data))
{
	struct ubase_adev *uadev;
	int ret;

	if (!adev || !comp_handler)
		return -EINVAL;

	uadev = container_of(adev, struct ubase_adev, adev);
	uadev->comp_notifier.notifier_call = comp_handler;
	ret = atomic_notifier_chain_register(&uadev->comp_nh,
					     &uadev->comp_notifier);
	if (ret)
		ubase_err(uadev->udev,
			  "failed to register comp notifier chain, ret = %d.\n",
			  ret);

	return ret;
}
EXPORT_SYMBOL(ubase_comp_register);

void ubase_comp_unregister(struct auxiliary_device *adev)
{
	struct ubase_adev *uadev;
	int ret;

	if (!adev)
		return;

	uadev = container_of(adev, struct ubase_adev, adev);

	ret = atomic_notifier_chain_unregister(&uadev->comp_nh,
					       &uadev->comp_notifier);
	if (ret)
		ubase_err(uadev->udev,
			  "failed to unregister comp notifier chain, ret = %d.\n",
			  ret);
}
EXPORT_SYMBOL(ubase_comp_unregister);

static void __ubase_unregister_ae_event(struct ubase_dev *udev, int num)
{
	int i;

	for (i = 0; i < num; i++)
		__ubase_event_unregister(udev, &udev->irq_table.aeq.cb[i]);
}

void ubase_unregister_ae_event(struct ubase_dev *udev)
{
	__ubase_unregister_ae_event(udev, UBASE_AE_LEVEL_NUM);
}

int ubase_register_ae_event(struct ubase_dev *udev)
{
	struct ubase_event_nb ubase_ae_nbs[UBASE_AE_LEVEL_NUM] = {};
	struct ubase_aeq *aeq = &udev->irq_table.aeq;
	int i, ret;

	for (i = 0; i < ARRAY_SIZE(ubase_ae_nbs); i++) {
		aeq->cb[i] = ubase_ae_nbs[i];
		ret = __ubase_event_register(udev, &aeq->cb[i]);
		if (ret) {
			ubase_err(udev,
				  "failed to register asyn event[%u], ret = %d",
				  aeq->cb[i].event_type, ret);
			__ubase_unregister_ae_event(udev, i);
			return ret;
		}
	}

	return 0;
}

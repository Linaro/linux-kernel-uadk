// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved.
 *
 */

#include <linux/delay.h>

#include "ubase_cmd.h"

int __ubase_register_crq_event(struct ubase_dev *udev,
			       struct ubase_crq_event_nb *nb)
{
	struct ubase_crq_event_nbs *nbs, *tmp, *new_nbs;
	struct ubase_crq_table *crq_table;
	int ret;

	crq_table = &udev->crq_table;
	mutex_lock(&crq_table->lock);
	list_for_each_entry_safe(nbs, tmp, &crq_table->nbs.list, list) {
		if (unlikely(nbs->nb.opcode == nb->opcode)) {
			ret = -EEXIST;
			goto err_crq_register;
		}
	}

	new_nbs = kzalloc(sizeof(*new_nbs), GFP_KERNEL);
	if (!new_nbs) {
		ret = -ENOMEM;
		goto err_crq_register;
	}

	new_nbs->nb = *nb;
	list_add_tail(&new_nbs->list, &crq_table->nbs.list);
	mutex_unlock(&crq_table->lock);

	return 0;

err_crq_register:
	mutex_unlock(&crq_table->lock);

	ubase_err(udev, "failed to register crq event, opcode = 0x%x, ret = %d.\n",
		  nb->opcode, ret);
	return ret;
}

int ubase_register_crq_event(struct auxiliary_device *aux_dev,
			     struct ubase_crq_event_nb *nb)
{
	struct ubase_dev *udev;

	if (!aux_dev || !nb || !nb->crq_handler)
		return -EINVAL;

	udev = __ubase_get_udev_by_adev(aux_dev);
	return __ubase_register_crq_event(udev, nb);
}
EXPORT_SYMBOL(ubase_register_crq_event);

void __ubase_unregister_crq_event(struct ubase_dev *udev, u16 opcode)
{
	struct ubase_crq_event_nbs *nbs, *tmp;
	struct ubase_crq_table *crq_table;

	crq_table = &udev->crq_table;
	mutex_lock(&crq_table->lock);
	list_for_each_entry_safe(nbs, tmp, &crq_table->nbs.list, list) {
		if (nbs->nb.opcode == opcode) {
			list_del(&nbs->list);
			kfree(nbs);
			break;
		}
	}
	mutex_unlock(&crq_table->lock);
}

void ubase_unregister_crq_event(struct auxiliary_device *aux_dev, u16 opcode)
{
	struct ubase_dev *udev;

	if (!aux_dev)
		return;

	udev = __ubase_get_udev_by_adev(aux_dev);
	__ubase_unregister_crq_event(udev, opcode);
}
EXPORT_SYMBOL(ubase_unregister_crq_event);

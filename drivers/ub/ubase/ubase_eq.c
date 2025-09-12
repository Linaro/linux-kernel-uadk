// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved.
 *
 */

#include <linux/interrupt.h>
#include <ub/ubase/ubase_comm_eq.h>

#include "ubase_dev.h"
#include "ubase_eq.h"

void ubase_enable_misc_vector(struct ubase_dev *udev, bool enable)
{
	ubase_write_dev(&udev->hw, UBASE_MISC_VECTOR_REG_OFFSET,
			enable ? 0 : 1);
}

static int ubase_reg_event_handler(struct ubase_dev *udev)
{
	int ret = IRQ_HANDLED;

	ubase_enable_misc_vector(udev, false);

	ubase_enable_misc_vector(udev, true);

	return ret;
}

static irqreturn_t ubase_misc_int_handler(int irq, void *data)
{
	struct ubase_dev *udev = (struct ubase_dev *)data;

	return IRQ_RETVAL(ubase_reg_event_handler(udev));
}

static int ubase_request_misc_irq(struct ubase_dev *udev)
{
	struct ubase_irq_table *irq_table = &udev->irq_table;
	struct ubase_irq *irq;
	int ret;

	if (ubase_ubus_irq_vector(udev->dev, 0) == -EOPNOTSUPP)
		return 0;

	irq = irq_table->irqs[UBASE_MISC_IRQ_INDEX];
	snprintf(irq->name, UBASE_INT_NAME_LEN, "ubase%d-%s-%d", udev->dev_id,
		 "misc", 0);
	ret = request_irq(irq->irqn, ubase_misc_int_handler, 0, irq->name, udev);
	if (ret) {
		ubase_err(udev,
			  "failed to request misc irq, ret = %d.\n", ret);
		return ret;
	}

	ubase_enable_misc_vector(udev, true);

	return ret;
}

static void ubase_free_misc_irq(struct ubase_dev *udev)
{
	struct ubase_irq_table *irq_table = &udev->irq_table;
	struct ubase_irq *irq;

	if (!irq_table->irqs)
		return;

	ubase_enable_misc_vector(udev, false);

	irq = irq_table->irqs[UBASE_MISC_IRQ_INDEX];
	if (ubase_ubus_irq_vector(udev->dev, 0) != -EOPNOTSUPP)
		free_irq(irq->irqn, udev);
}

static int ubase_irq_init(struct ubase_dev *udev)
{
	struct ubase_irq_table *irq_table = &udev->irq_table;
	u32 irqs_num = irq_table->irqs_num;
	struct ubase_irq **irqs;
	int ret;
	u32 i;

	irqs = kcalloc(irqs_num, sizeof(struct ubase_irq *), GFP_KERNEL);
	if (!irqs) {
		ubase_err(udev, "failed to alloc irqs.\n");
		return -ENOMEM;
	}

	for (i = 0; i < irqs_num; i++) {
		irqs[i] = kzalloc(sizeof(struct ubase_irq), GFP_KERNEL);
		if (!irqs[i]) {
			ubase_err(udev, "failed to alloc ubase irq[%u].\n", i);
			ret = -ENOMEM;
			goto err_alloc_ubase_irq;
		}

		if (ubase_ubus_irq_vector(udev->dev, 0) == -EOPNOTSUPP)
			continue;

		irqs[i]->irqn = ubase_ubus_irq_vector(udev->dev, i);
		if (irqs[i]->irqn < 0) {
			ubase_err(udev,
				  "failed to get irq[%u] num, err irq num = %d.\n",
				  i, irqs[i]->irqn);
			ret = irqs[i]->irqn;
			kfree(irqs[i]);
			goto err_alloc_ubase_irq;
		}
	}
	irq_table->irqs = irqs;

	return 0;

err_alloc_ubase_irq:
	for (; i > 0; i--)
		kfree(irqs[i - 1]);
	kfree(irqs);
	irq_table->irqs = NULL;

	return ret;
}

static void ubase_irq_uninit(struct ubase_dev *udev)
{
	struct ubase_irq_table *irq_table = &udev->irq_table;
	u32 i;

	if (!irq_table->irqs)
		return;

	for (i = 0; i < irq_table->irqs_num; i++)
		kfree(irq_table->irqs[i]);
	kfree(irq_table->irqs);
	irq_table->irqs = NULL;
}

static int ubase_request_irq(struct ubase_dev *udev)
{
	int ret;

	ret = ubase_request_misc_irq(udev);
	if (ret) {
		ubase_err(udev,
			  "failed to request ubase misc irq, ret = %d.\n", ret);
		return ret;
	}

	return 0;
}

static void ubase_free_irq(struct ubase_dev *udev)
{
	ubase_free_misc_irq(udev);
}

int ubase_irq_table_init(struct ubase_dev *udev)
{
	struct ubase_irq_table *irq_table = &udev->irq_table;
	int i, j, ret;

	if (!test_bit(UBASE_STATE_RST_HANDLING_B, &udev->state_bits)) {
		for (i = 0; i < UBASE_DRV_MAX; i++) {
			for (j = 0; j < UBASE_EVENT_TYPE_MAX; j++)
				BLOCKING_INIT_NOTIFIER_HEAD(&irq_table->nh[i][j]);
		}
		mutex_init(&udev->irq_table.ceq_lock);
	}

	ret = ubase_ubus_irq_vectors_alloc(udev->dev);
	if (ret) {
		ubase_err(udev, "failed to alloc irq vectors, ret = %d.\n",
			  ret);
		goto err_irq_res_init;
	}

	ret = ubase_irq_init(udev);
	if (ret) {
		ubase_err(udev, "failed to init ubase irq, ret = %d.\n", ret);
		goto err_irq_init;
	}

	ret = ubase_request_irq(udev);
	if (ret) {
		ubase_err(udev, "failed to request ubase irq, ret = %d.\n",
			  ret);
		goto err_request_irq;
	}

	clear_bit(UBASE_STATE_IRQ_INVALID_B, &udev->state_bits);

	return 0;

err_request_irq:
	ubase_irq_uninit(udev);
err_irq_init:
	ubase_ubus_irq_vectors_free(udev->dev);
err_irq_res_init:
	if (!test_bit(UBASE_STATE_RST_HANDLING_B, &udev->state_bits))
		mutex_destroy(&udev->irq_table.ceq_lock);

	return ret;
}

void ubase_irq_table_free(struct ubase_dev *udev)
{
	if (test_and_set_bit(UBASE_STATE_IRQ_INVALID_B, &udev->state_bits))
		return;

	ubase_free_irq(udev);
	ubase_irq_uninit(udev);
	ubase_ubus_irq_vectors_free(udev->dev);
}

void ubase_irq_table_uninit(struct ubase_dev *udev)
{
	ubase_irq_table_free(udev);
}

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

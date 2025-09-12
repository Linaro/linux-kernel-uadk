// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved.
 *
 */

#include <linux/kernel.h>
#include <ub/ubus/ubus.h>

#include "debugfs/ubase_debugfs.h"
#include "ubase_cmd.h"
#include "ubase_hw.h"
#include "ubase_mailbox.h"
#include "ubase_dev.h"

#define UBASE_PERIOD_100MS 100

static int ubase_debug;
module_param_named(debug, ubase_debug, int, 0644);
MODULE_PARM_DESC(debug, "enable ubase debug log, 0:disable, others:enable, default:0");

static DEFINE_IDA(ubase_adev_ida);

bool ubase_dev_urma_supported(struct ubase_dev *udev)
{
	struct ub_entity *ue = container_of(udev->dev, struct ub_entity, dev);

	switch (uent_device(ue)) {
	case UBASE_DEV_ID_K_0_URMA_MUE:
	case UBASE_DEV_ID_K_0_URMA_UE:
	case UBASE_DEV_ID_A_0_URMA_MUE:
	case UBASE_DEV_ID_A_0_URMA_UE:
	case UBASE_DEV_ID_A_0_UBOE_MUE:
	case UBASE_DEV_ID_A_0_UBOE_UE:
		break;
	default:
		return false;
	}

	return true;
}

bool ubase_dev_unic_supported(struct ubase_dev *udev)
{
	struct ub_entity *ue = container_of(udev->dev, struct ub_entity, dev);

	switch (uent_device(ue)) {
	case UBASE_DEV_ID_K_0_URMA_MUE:
	case UBASE_DEV_ID_A_0_URMA_MUE:
	case UBASE_DEV_ID_A_0_UBOE_MUE:
		break;
	default:
		return false;
	}

	return !ubase_get_cap_bit(udev, UBASE_SUPPORT_UNIC_DISABLE_B);
}

bool ubase_dev_cdma_supported(struct ubase_dev *udev)
{
	struct ub_entity *ue = container_of(udev->dev, struct ub_entity, dev);

	switch (uent_device(ue)) {
	case UBASE_DEV_ID_K_0_CDMA_MUE:
	case UBASE_DEV_ID_K_0_CDMA_UE:
	case UBASE_DEV_ID_A_0_CDMA_MUE:
	case UBASE_DEV_ID_A_0_CDMA_UE:
		break;
	default:
		return false;
	}

	return true;
}

bool ubase_dev_pmu_supported(struct ubase_dev *udev)
{
	struct ub_entity *ue = container_of(udev->dev, struct ub_entity, dev);

	switch (uent_device(ue)) {
	case UBASE_DEV_ID_K_0_PMU_MUE:
	case UBASE_DEV_ID_K_0_PMU_UE:
	case UBASE_DEV_ID_A_0_PMU_MUE:
	case UBASE_DEV_ID_A_0_PMU_UE:
		break;
	default:
		return false;
	}

	return true;
}

bool ubase_dev_fwctl_supported(struct ubase_dev *udev)
{
	return ubase_dev_pmu_supported(udev);
}

static struct ubase_adev_device {
	const char *suffix;
	bool (*is_supported)(struct ubase_dev *dev);
} ubase_adev_devices[UBASE_DRV_MAX] = {
	[UBASE_DRV_UNIC] = {
		.suffix = "unic",
		.is_supported = &ubase_dev_unic_supported
	},
	[UBASE_DRV_UDMA] = {
		.suffix = "udma",
		.is_supported = &ubase_dev_udma_supported
	},
	[UBASE_DRV_CDMA] = {
		.suffix = "cdma",
		.is_supported = &ubase_dev_cdma_supported
	},
	[UBASE_DRV_FWCTL] = {
		.suffix = "fwctl",
		.is_supported = &ubase_dev_fwctl_supported
	},
	[UBASE_DRV_PMU] = {
		.suffix = "pmu",
		.is_supported = &ubase_dev_pmu_supported
	},
	[UBASE_DRV_UVB] = {
		.suffix = "uvb",
		.is_supported = &ubase_dev_uvb_supported
	},
};

int ubase_adev_idx_alloc(void)
{
	return ida_alloc(&ubase_adev_ida, GFP_KERNEL);
}

void ubase_adev_idx_free(int id)
{
	ida_free(&ubase_adev_ida, id);
}

static void ubase_port_handler(struct ubase_dev *udev, bool link_up)
{
	struct ubase_adev *uadev;
	int i;

	if (!test_bit(UBASE_STATE_INITED_B, &udev->state_bits))
		return;

	mutex_lock(&udev->priv.uadev_lock);
	for (i = 0; i < UBASE_DRV_MAX; i++) {
		uadev = udev->priv.uadev[i];
		if (!uadev)
			continue;

		mutex_lock(&uadev->port_lock);
		if (uadev->port_handler)
			uadev->port_handler(&uadev->adev, link_up);
		mutex_unlock(&uadev->port_lock);
	}
	mutex_unlock(&udev->priv.uadev_lock);
}

void ubase_port_down(struct ubase_dev *udev)
{
	ubase_port_handler(udev, 0);
}

void ubase_port_up(struct ubase_dev *udev)
{
	ubase_port_handler(udev, 1);
}

static void ubase_comm_adev_release(struct device *dev)
{
	struct ubase_adev *ubase_adev =
				container_of(dev, struct ubase_adev, adev.dev);

	kfree(ubase_adev);
}

static struct ubase_adev *ubase_add_one_adev(struct ubase_dev *udev, int idx)
{
	struct ubase_adev *uadev;
	int ret;

	uadev = kzalloc(sizeof(struct ubase_adev), GFP_KERNEL);
	if (!uadev) {
		ubase_err(udev, "failed to alloc auxiliary device(%s.%d).\n",
			  ubase_adev_devices[idx].suffix, udev->dev_id);
		return ERR_PTR(-ENOMEM);
	}

	uadev->adev.name = ubase_adev_devices[idx].suffix;
	uadev->adev.id = (u32)udev->dev_id;
	uadev->adev.dev.parent = udev->dev;
	uadev->adev.dev.release = ubase_comm_adev_release;
	uadev->idx = idx;
	uadev->udev = udev;
	ATOMIC_INIT_NOTIFIER_HEAD(&uadev->comp_nh);

	ret = auxiliary_device_init(&uadev->adev);
	if (ret) {
		kfree(uadev);
		ubase_err(udev,
			  "failed to init auxiliary device(%s.%d), ret = %d\n",
			  ubase_adev_devices[idx].suffix, udev->dev_id, ret);
		return ERR_PTR(ret);
	}

	ret = auxiliary_device_add(&uadev->adev);
	if (ret) {
		auxiliary_device_uninit(&uadev->adev);
		ubase_err(udev,
			  "failed to add auxiliary device(%s.%d), ret = %d\n",
			  ubase_adev_devices[idx].suffix, udev->dev_id, ret);
		return ERR_PTR(ret);
	}

	mutex_init(&uadev->virt_lock);
	mutex_init(&uadev->port_lock);
	mutex_init(&uadev->reset_lock);
	mutex_init(&uadev->activate_lock);

	return uadev;
}

static void ubase_del_one_adev(struct ubase_dev *udev, int idx)
{
	struct ubase_priv *priv = &udev->priv;
	struct ubase_adev *uadev;

	uadev = priv->uadev[idx];

	mutex_destroy(&uadev->activate_lock);
	mutex_destroy(&uadev->reset_lock);
	mutex_destroy(&uadev->port_lock);
	mutex_destroy(&uadev->virt_lock);
	auxiliary_device_delete(&uadev->adev);
	auxiliary_device_uninit(&uadev->adev);
	priv->uadev[idx] = NULL;
}

static int ubase_init_aux_devices(struct ubase_dev *udev)
{
	struct ubase_priv *priv = &udev->priv;
	int i, ret;

	for (i = 0; i < ARRAY_SIZE(ubase_adev_devices); i++) {
		if (priv->uadev[i])
			continue;

		if (!ubase_adev_devices[i].is_supported ||
		    !ubase_adev_devices[i].is_supported(udev))
			continue;

		priv->uadev[i] = ubase_add_one_adev(udev, i);
		if (IS_ERR(priv->uadev[i])) {
			ret = PTR_ERR(priv->uadev[i]);
			priv->uadev[i] = NULL;
			ubase_err(udev,
				  "failed to load auxiliary device(%s.%d)\n",
				  ubase_adev_devices[i].suffix, udev->dev_id);
			goto err_add_aux_dev;
		}
	}

	mutex_init(&udev->priv.uadev_lock);

	return 0;

err_add_aux_dev:
	for (; i >= 0; i--) {
		if (!priv->uadev[i])
			continue;

		ubase_del_one_adev(udev, i);
	}

	return ret;
}

static void ubase_uninit_aux_devices(struct ubase_dev *udev)
{
	struct ubase_priv *priv = &udev->priv;
	int i;

	mutex_lock(&priv->uadev_lock);
	for (i = ARRAY_SIZE(ubase_adev_devices) - 1; i >= 0; i--) {
		if (!priv->uadev[i])
			continue;

		ubase_del_one_adev(udev, i);
	}
	mutex_unlock(&priv->uadev_lock);

	mutex_destroy(&udev->priv.uadev_lock);
}

static void ubase_cancel_period_service_task(struct ubase_dev *udev)
{
	if (udev->period_service_task.service_task.work.func)
		cancel_delayed_work_sync(&udev->period_service_task.service_task);
}

static int ubase_enable_period_service_task(struct ubase_dev *udev)
{
	struct ubase_delay_work *period_work = &udev->period_service_task;
	unsigned long delta;

	delta = round_jiffies_relative(msecs_to_jiffies(UBASE_PERIOD_100MS));
	mod_delayed_work(udev->ubase_period_wq,
			 &period_work->service_task,
			 delta);

	return 0;
}

static void ubase_period_service_task(struct work_struct *work)
{
#define UBASE_STATS_TIMER_INTERVAL		(300000 / (UBASE_PERIOD_100MS))
#define UBASE_QUERY_SL_TIMER_INTERVAL		(1000 / (UBASE_PERIOD_100MS))

	struct ubase_delay_work *ubase_work =
		container_of(work, struct ubase_delay_work, service_task.work);
	struct ubase_dev *udev = container_of(ubase_work, struct ubase_dev,
					      period_service_task);

	if (test_bit(UBASE_STATE_DISABLED_B, &udev->state_bits)) {
		ubase_enable_period_service_task(udev);
		return;
	}

	udev->serv_proc_cnt++;
	ubase_enable_period_service_task(udev);
}

static void ubase_init_delayed_work(struct ubase_dev *udev)
{
	INIT_DELAYED_WORK(&udev->period_service_task.service_task,
			  ubase_period_service_task);
}

static int ubase_wq_init(struct ubase_dev *udev)
{
#define UBASE_ALLOC_WQ(name)	alloc_workqueue("%s", WQ_UNBOUND, 0, name)

	udev->ubase_wq = UBASE_ALLOC_WQ("ubase");
	if (!udev->ubase_wq) {
		ubase_err(udev, "failed to alloc ubase workqueue.\n");
		goto err_alloc_ubase_wq;
	}

	udev->ubase_async_wq = UBASE_ALLOC_WQ("ubase_async_service");
	if (!udev->ubase_async_wq) {
		ubase_err(udev, "failed to alloc ubase async workqueue.\n");
		goto err_alloc_ubase_async_wq;
	}

	udev->ubase_reset_wq = UBASE_ALLOC_WQ("ubase_reset_service");
	if (!udev->ubase_reset_wq) {
		ubase_err(udev, "failed to alloc ubase reset workqueue.\n");
		goto err_alloc_ubase_reset_wq;
	}

	udev->ubase_period_wq = UBASE_ALLOC_WQ("ubase_period_service");
	if (!udev->ubase_period_wq) {
		ubase_err(udev, "failed to alloc ubase period workqueue.\n");
		goto err_alloc_ubase_period_wq;
	}

	udev->ubase_arq_wq = UBASE_ALLOC_WQ("ubase_arq_service");
	if (!udev->ubase_arq_wq) {
		ubase_err(udev, "failed to alloc ubase arq workqueue.\n");
		goto err_alloc_ubase_arq_wq;
	}

	ubase_init_delayed_work(udev);
	return 0;

err_alloc_ubase_arq_wq:
	destroy_workqueue(udev->ubase_period_wq);
err_alloc_ubase_period_wq:
	destroy_workqueue(udev->ubase_reset_wq);
err_alloc_ubase_reset_wq:
	destroy_workqueue(udev->ubase_async_wq);
err_alloc_ubase_async_wq:
	destroy_workqueue(udev->ubase_wq);
err_alloc_ubase_wq:
	return -ENOMEM;
}

static void ubase_wq_uninit(struct ubase_dev *udev)
{
	destroy_workqueue(udev->ubase_arq_wq);
	destroy_workqueue(udev->ubase_period_wq);
	destroy_workqueue(udev->ubase_reset_wq);
	destroy_workqueue(udev->ubase_async_wq);
	destroy_workqueue(udev->ubase_wq);
}

static struct ubase_crq_event_nb ubase_crq_events[] = {
};

static void ubase_unregister_cmdq_crq_event(struct ubase_dev *udev)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(ubase_crq_events); i++)
		__ubase_unregister_crq_event(udev, ubase_crq_events[i].opcode);
}

static int ubase_register_cmdq_crq_event(struct ubase_dev *udev)
{
	int ret, i;

	for (i = 0; i < ARRAY_SIZE(ubase_crq_events); i++) {
		ubase_crq_events[i].back = udev;

		ret = __ubase_register_crq_event(udev, &ubase_crq_events[i]);
		if (ret) {
			ubase_err(udev,
				  "failed to register crq event[%d], ret = %d.\n",
				  i, ret);
			goto err_reg_event;
		}
	}

	return 0;

err_reg_event:
	for (i = i - 1; i >= 0; i--)
		__ubase_unregister_crq_event(udev, ubase_crq_events[i].opcode);
	return ret;
}

static const struct ubase_init_function ubase_init_func_map[] = {
	{
		"init work queue", UBASE_SUP_ALL, 0,
		ubase_wq_init, ubase_wq_uninit
	},
	{
		"init cmd queue", UBASE_SUP_ALL, 1,
		ubase_cmd_init, ubase_cmd_uninit
	},
	{
		"query dev res", UBASE_SUP_ALL, 0,
		ubase_query_dev_res, NULL
	},
	{
		"init mailbox", UBASE_SUP_NO_PMU, 0,
		ubase_mbox_cmd_init, ubase_mbox_cmd_uninit
	},
	{
		"query chip info", UBASE_SUP_ALL, 0,
		ubase_query_chip_info, NULL
	},
	{
		"query controller_info", UBASE_SUP_NO_PMU, 0,
		ubase_query_controller_info, NULL
	},
	{
		"query hw oor caps", UBASE_SUP_NO_PMU, 0,
		NULL, NULL
	},
	{
		"init irq table", UBASE_SUP_NO_PMU, 1,
		ubase_irq_table_init, ubase_irq_table_uninit
	},
	{
		"init ctrl queue", UBASE_SUP_NO_PMU, 1,
		NULL, NULL
	},
	{
		"register aeq event", UBASE_SUP_NO_PMU, 0,
		ubase_register_ae_event, ubase_unregister_ae_event
	},
	{
		"register cmdq crq event", UBASE_SUP_NO_PMU, 0,
		ubase_register_cmdq_crq_event, ubase_unregister_cmdq_crq_event
	},
	{
		"register ctrlq crq event", UBASE_SUP_NO_PMU, 0,
		NULL, NULL
	},
	{
		"init qos", UBASE_SUP_ALL, 0,
		NULL, NULL
	},
	{
		"prealloc memory", UBASE_SUP_UDMA, 1,
		NULL, NULL
	},
	{
		"init ue", UBASE_SUP_NO_PMU, 0,
		NULL, NULL
	},
	{
		"init hw", UBASE_SUP_NO_PMU, 1,
		NULL, NULL
	},
	{
		"init debugfs", UBASE_SUP_ALL, 0,
		ubase_dbg_init, ubase_dbg_uninit
	},
	{
		"init auxiliary devices", UBASE_SUP_ALL, 0,
		ubase_init_aux_devices, ubase_uninit_aux_devices
	},
	{
		"enable period service task", UBASE_SUP_NO_PMU, 0,
		ubase_enable_period_service_task, ubase_cancel_period_service_task
	},
	{
		"enable ce irq", UBASE_SUP_NO_PMU, 1,
		ubase_enable_ce_irqs, ubase_disable_ce_irqs
	},
};

static bool ubase_init_func_support(struct ubase_dev *udev, u32 support)
{
	return (((support & UBASE_SUP_UNIC) && ubase_dev_unic_supported(udev)) ||
		((support & UBASE_SUP_UDMA) && ubase_dev_udma_supported(udev)) ||
		((support & UBASE_SUP_CDMA) && ubase_dev_cdma_supported(udev)) ||
		((support & UBASE_SUP_PMU) && ubase_dev_pmu_supported(udev)));
}

int ubase_dev_init(struct ubase_dev *udev)
{
	int i, ret;

	for (i = 0; i < ARRAY_SIZE(ubase_init_func_map); i++) {
		if (!ubase_init_func_support(udev,
			ubase_init_func_map[i].support_devs))
			continue;

		if (ubase_init_func_map[i].init_func) {
			ret = ubase_init_func_map[i].init_func(udev);
			if (ret) {
				ubase_err(udev, "failed to %s, ret = %d.\n",
					  ubase_init_func_map[i].err_msg, ret);
				goto err_init;
			}
		}
	}

	set_bit(UBASE_STATE_INITED_B, &udev->state_bits);

	return 0;

err_init:
	for (i -= 1; i >= 0; i--) {
		if (!ubase_init_func_support(udev,
					     ubase_init_func_map[i].support_devs))
			continue;

		if (ubase_init_func_map[i].uninit_func)
			ubase_init_func_map[i].uninit_func(udev);
	}

	return ret;
}

void ubase_dev_uninit(struct ubase_dev *udev)
{
	int i;

	if (udev->service_task.service_task.work.func)
		cancel_delayed_work_sync(&udev->service_task.service_task);
	flush_workqueue(udev->ubase_async_wq);

	for (i = ARRAY_SIZE(ubase_init_func_map) - 1; i >= 0; i--) {
		if (!ubase_init_func_support(udev,
					     ubase_init_func_map[i].support_devs))
			continue;

		if (ubase_init_func_map[i].uninit_func)
			ubase_init_func_map[i].uninit_func(udev);
	}
}

bool ubase_adev_ubl_supported(struct auxiliary_device *adev)
{
	if (!adev)
		return false;

	return ubase_dev_ubl_supported(__ubase_get_udev_by_adev(adev));
}
EXPORT_SYMBOL(ubase_adev_ubl_supported);

bool ubase_adev_eth_mac_supported(struct auxiliary_device *adev)
{
	if (!adev)
		return false;

	return ubase_dev_eth_mac_supported(__ubase_get_udev_by_adev(adev));
}
EXPORT_SYMBOL(ubase_adev_eth_mac_supported);

struct ubase_resource_space *ubase_get_io_base(struct auxiliary_device *adev)
{
	if (!adev)
		return NULL;

	return &__ubase_get_udev_by_adev(adev)->hw.io_base;
}
EXPORT_SYMBOL(ubase_get_io_base);

struct ubase_resource_space *ubase_get_mem_base(struct auxiliary_device *adev)
{
	if (!adev)
		return NULL;

	return &__ubase_get_udev_by_adev(adev)->hw.mem_base;
}
EXPORT_SYMBOL(ubase_get_mem_base);

struct ubase_caps *ubase_get_dev_caps(struct auxiliary_device *adev)
{
	if (!adev)
		return NULL;

	return &__ubase_get_udev_by_adev(adev)->caps.dev_caps;
}
EXPORT_SYMBOL(ubase_get_dev_caps);

struct ubase_adev_caps *ubase_get_udma_caps(struct auxiliary_device *adev)
{
	struct ubase_dev *udev;

	if (!adev)
		return NULL;

	udev = __ubase_get_udev_by_adev(adev);

	return &udev->caps.udma_caps;
}
EXPORT_SYMBOL(ubase_get_udma_caps);

struct ubase_adev_caps *ubase_get_cdma_caps(struct auxiliary_device *adev)
{
	return ubase_get_udma_caps(adev);
}
EXPORT_SYMBOL(ubase_get_cdma_caps);

struct ubase_adev_caps *ubase_get_unic_caps(struct auxiliary_device *adev)
{
	struct ubase_dev *udev;

	if (!adev)
		return NULL;

	udev = __ubase_get_udev_by_adev(adev);

	return &udev->caps.unic_caps;
}
EXPORT_SYMBOL(ubase_get_unic_caps);

bool ubase_dbg_default(void)
{
	return ubase_debug;
}

static int ubase_query_bus_eid(struct ubase_dev *udev, struct ubase_bus_eid *eid)
{
	struct ubase_query_ueid_cmd resp = {0};
	struct ubase_cmd_buf in, out;
	int i, ret;

	__ubase_fill_inout_buf(&in, UBASE_OPC_QUERY_BUS_EID, true, 0, NULL);
	__ubase_fill_inout_buf(&out, UBASE_OPC_QUERY_BUS_EID, false,
			       sizeof(resp), &resp);

	ret = __ubase_cmd_send_inout(udev, &in, &out);
	if (ret) {
		ubase_err(udev, "failed to query bus eid, ret = %d.\n", ret);
		return ret;
	}

	for (i = 0; i < UBASE_BUS_EID_LEN; ++i)
		eid->eid[i] = le32_to_cpu(resp.ueid[i]);

	return 0;
}

static int __ubase_get_bus_eid(struct ubase_dev *udev, struct ubase_bus_eid *eid)
{
	return ubase_query_bus_eid(udev, eid);
}

int ubase_get_bus_eid(struct auxiliary_device *adev, struct ubase_bus_eid *eid)
{
	struct ubase_dev *udev;

	if (!adev || !eid)
		return -EINVAL;

	udev = __ubase_get_udev_by_adev(adev);

	return __ubase_get_bus_eid(udev, eid);
}
EXPORT_SYMBOL(ubase_get_bus_eid);

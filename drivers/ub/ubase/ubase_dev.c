// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved.
 *
 */

#include <linux/kernel.h>
#include <ub/ubus/ubus.h>

#include "debugfs/ubase_debugfs.h"
#include "ubase_cmd.h"
#include "ubase_ctrlq.h"
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

void ubase_service_task(struct work_struct *work)
{
	struct ubase_delay_work *ubase_work =
		container_of(work, struct ubase_delay_work, service_task.work);

	ubase_crq_service_task(ubase_work);
	ubase_ctrlq_service_task(ubase_work);
	ubase_ctrlq_clean_service_task(ubase_work);
}

static void ubase_init_delayed_work(struct ubase_dev *udev)
{
	INIT_DELAYED_WORK(&udev->service_task.service_task, ubase_service_task);
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

static int ubase_handle_ue2ue_ctrlq_req(struct ubase_dev *udev,
					struct ubase_ue2ue_ctrlq_head *cmd,
					u32 len)
{
	struct ubase_ctrlq_base_block *head = (struct ubase_ctrlq_base_block *)(cmd + 1);
	u16 mbx_ue_id = le16_to_cpu(cmd->head.mbx_ue_id);
	struct ubase_ctrlq_ue_info ue_info;
	struct ubase_ctrlq_msg msg = {0};
	int ret;

	if (!ubase_mbx_ue_id_is_valid(mbx_ue_id, udev)) {
		ubase_err(udev, "ubase ue2ue ctrlq req mbx ue id = %u error.\n",
			  mbx_ue_id);
		return -EINVAL;
	}

	if (cmd->in_size > (len - (sizeof(*cmd) + UBASE_CTRLQ_HDR_LEN))) {
		ubase_err(udev, "ubase ue2ue cmd len = %u error.\n", cmd->in_size);
		return -EINVAL;
	}

	if (cmd->in_size > (len - (sizeof(*cmd) + UBASE_CTRLQ_HDR_LEN))) {
		ubase_err(udev, "ubase e2e cmd len = %u error.\n", cmd->in_size);
		return -EINVAL;
	}

	msg.service_ver = head->service_ver;
	msg.service_type = head->service_type;
	msg.opcode = head->opcode;
	msg.need_resp = cmd->need_resp;
	msg.is_resp = cmd->is_resp;
	msg.resp_seq = cmd->seq;
	msg.in = (u8 *)head + UBASE_CTRLQ_HDR_LEN;
	msg.in_size = cmd->in_size;
	msg.out = NULL;
	msg.out_size = 0;

	ue_info.bus_ue_id = le16_to_cpu(cmd->head.bus_ue_id);
	ue_info.seq = cmd->seq;
	ue_info.mbx_ue_id = mbx_ue_id;

	ret = __ubase_ctrlq_send(udev, &msg, &ue_info);
	if (ret)
		ubase_err(udev, "failed to send opc(0x%x) ctrlq, ret = %d.\n",
			  head->opcode, ret);

	return ret;
}

static int ubase_handle_ue2ue_ctrlq_event(struct ubase_dev *udev, void *data,
					  u32 len)
{
	struct ubase_ue2ue_ctrlq_head *cmd = data;
	struct ubase_ctrlq_base_block *head;
	u16 data_len;

	if (len < (sizeof(*cmd) + UBASE_CTRLQ_HDR_LEN)) {
		ubase_err(udev, "invalid ue2ue ctrlq event len(%u).\n", len);
		return -EINVAL;
	}

	if (ubase_dev_ctrlq_supported(udev))
		return ubase_handle_ue2ue_ctrlq_req(udev, cmd, len);

	head = (struct ubase_ctrlq_base_block *)(cmd + 1);
	data_len = len - sizeof(*cmd) - UBASE_CTRLQ_HDR_LEN;
	ubase_ctrlq_handle_crq_msg(udev, head, cmd->seq,
				   (u8 *)head + UBASE_CTRLQ_HDR_LEN, data_len);

	return 0;
}

struct ubase_ue2ue_event_handler {
	u16 sub_cmd;
	int (*event_handler)(struct ubase_dev *udev, void *data, u32 len);
} ubase_ue2ue_events[] = {
	{ UBASE_UE2UE_CTRLQ_MSG, ubase_handle_ue2ue_ctrlq_event },
};

static int ubase_handle_ue2ue_event(void *dev, void *data, u32 len)
{
	struct ubase_ue2ue_common_head *head = data;
	struct ubase_dev *udev = dev;
	int i;

	for (i = 0; i < ARRAY_SIZE(ubase_ue2ue_events); i++) {
		if (ubase_ue2ue_events[i].sub_cmd == head->sub_cmd)
			return ubase_ue2ue_events[i].event_handler(udev, data,
								   len);
	}

	ubase_warn(udev, "unknown ubase ue2ue event, sub_cmd = %u.\n",
		   head->sub_cmd);

	return 0;
}

static struct ubase_crq_event_nb ubase_crq_events[] = {
	{
		.opcode = UBASE_OPC_UE2UE_UBASE,
		.crq_handler = ubase_handle_ue2ue_event,
	},
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
		ubase_query_hw_oor_caps, NULL
	},
	{
		"init irq table", UBASE_SUP_NO_PMU, 1,
		ubase_irq_table_init, ubase_irq_table_uninit
	},
	{
		"init ctrl queue", UBASE_SUP_NO_PMU, 1,
		ubase_ctrlq_init, ubase_ctrlq_uninit
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
		ubase_qos_init, NULL
	},
	{
		"prealloc memory", UBASE_SUP_UDMA, 1,
		NULL, NULL
	},
	{
		"init ue", UBASE_SUP_NO_PMU, 0,
		ubase_ue_init, ubase_ue_uninit
	},
	{
		"init hw", UBASE_SUP_NO_PMU, 1,
		ubase_hw_init, ubase_hw_uninit
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

bool ubase_adev_ctrlq_supported(struct auxiliary_device *adev)
{
	if (!adev)
		return false;

	return ubase_dev_ctrlq_supported(__ubase_get_udev_by_adev(adev));
}
EXPORT_SYMBOL(ubase_adev_ctrlq_supported);

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

void ubase_virt_register(struct auxiliary_device *adev,
			 void (*virt_handler)(struct auxiliary_device *adev,
					      u16 bus_ue_id, bool is_en))
{
	struct ubase_adev *uadev;

	if (!adev || !virt_handler)
		return;

	uadev = container_of(adev, struct ubase_adev, adev);

	mutex_lock(&uadev->virt_lock);
	if (!uadev->virt_handler)
		uadev->virt_handler = virt_handler;
	mutex_unlock(&uadev->virt_lock);
}
EXPORT_SYMBOL(ubase_virt_register);

void ubase_virt_unregister(struct auxiliary_device *adev)
{
	struct ubase_adev *uadev;

	if (!adev)
		return;

	uadev = container_of(adev, struct ubase_adev, adev);

	mutex_lock(&uadev->virt_lock);
	uadev->virt_handler = NULL;
	mutex_unlock(&uadev->virt_lock);
}
EXPORT_SYMBOL(ubase_virt_unregister);

struct ubase_adev_caps *ubase_get_unic_caps(struct auxiliary_device *adev)
{
	struct ubase_dev *udev;

	if (!adev)
		return NULL;

	udev = __ubase_get_udev_by_adev(adev);

	return &udev->caps.unic_caps;
}
EXPORT_SYMBOL(ubase_get_unic_caps);

static bool ubase_add_ue_list(struct ubase_dev *udev, u16 bus_ue_id)
{
	struct ubase_ue_node *pos_node, *tmp_node, *new_node;

	new_node = kzalloc(sizeof(*new_node), GFP_KERNEL);
	if (!new_node) {
		ubase_err(udev, "failed to alloc ue node.\n");
		return false;
	}
	new_node->bus_ue_id = bus_ue_id;

	mutex_lock(&udev->ue_list_lock);
	list_for_each_entry_safe(pos_node, tmp_node, &udev->ue_list, list) {
		if (pos_node->bus_ue_id == bus_ue_id) {
			kfree(new_node);
			mutex_unlock(&udev->ue_list_lock);
			return false;
		} else if (bus_ue_id < pos_node->bus_ue_id) {
			list_add(&new_node->list, pos_node->list.prev);
			goto add_ue_end;
		}
	}
	list_add_tail(&new_node->list, &udev->ue_list);

add_ue_end:
	mutex_unlock(&udev->ue_list_lock);
	return true;
}

static bool ubase_del_ue_list(struct ubase_dev *udev, u16 bus_ue_id)
{
	struct ubase_ue_node *pos_node, *tmp_node;

	mutex_lock(&udev->ue_list_lock);
	list_for_each_entry_safe(pos_node, tmp_node, &udev->ue_list, list) {
		if (pos_node->bus_ue_id == bus_ue_id) {
			list_del(&pos_node->list);
			kfree(pos_node);
			mutex_unlock(&udev->ue_list_lock);
			return true;
		}
	}
	mutex_unlock(&udev->ue_list_lock);

	return false;
}

static bool ubase_modify_ue_list(struct ubase_dev *udev, u16 bus_ue_id, bool is_en)
{
	if (is_en)
		return ubase_add_ue_list(udev, bus_ue_id);
	else
		return ubase_del_ue_list(udev, bus_ue_id);
}

void ubase_virt_handler(struct ubase_dev *udev, u16 bus_ue_id, bool is_en)
{
	struct ubase_adev *uadev;
	int i;

	if (!ubase_modify_ue_list(udev, bus_ue_id, is_en))
		return;

	mutex_lock(&udev->priv.uadev_lock);
	for (i = 0; i < UBASE_DRV_MAX; i++) {
		uadev = udev->priv.uadev[i];
		if (!uadev)
			continue;

		mutex_lock(&uadev->virt_lock);
		if (uadev->virt_handler)
			uadev->virt_handler(&uadev->adev, bus_ue_id, is_en);
		mutex_unlock(&uadev->virt_lock);
	}
	mutex_unlock(&udev->priv.uadev_lock);
}

bool ubase_dbg_default(void)
{
	return ubase_debug;
}

struct ubase_adev_qos *ubase_get_adev_qos(struct auxiliary_device *adev)
{
	struct ubase_dev *udev;

	if (!adev)
		return NULL;

	udev = __ubase_get_udev_by_adev(adev);
	return &udev->qos;
}
EXPORT_SYMBOL(ubase_get_adev_qos);

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

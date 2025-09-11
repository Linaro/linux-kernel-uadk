// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved.
 *
 */

#define dev_fmt(fmt) "unic: (pid %d) " fmt, current->pid

#include <net/rtnetlink.h>
#include <linux/etherdevice.h>
#include <linux/netdevice.h>
#include <ub/ubase/ubase_comm_cmd.h>
#include <ub/ubase/ubase_comm_eq.h>
#include <ub/ubase/ubase_comm_hw.h>
#include <ub/ubase/ubase_comm_qos.h>
#include <ub/ubase/ubase_comm_ctrlq.h>

#include "unic_cmd.h"
#include "unic_crq.h"
#include "unic_dcbnl.h"
#include "unic_dev.h"
#include "unic_hw.h"
#include "unic_netdev.h"
#include "unic_qos_hw.h"
#include "unic_rack_ip.h"
#include "unic_event.h"

int unic_comp_handler(struct notifier_block *nb, unsigned long jfcn, void *data)
{
	struct auxiliary_device *adev = (struct auxiliary_device *)data;
	struct unic_dev *unic_dev = dev_get_drvdata(&adev->dev);
	struct unic_channels *channels = &unic_dev->channels;
	struct unic_cq *cq;
	u32 index;

	if (test_bit(UNIC_STATE_CHANNEL_INVALID, &unic_dev->state))
		return -EBUSY;

	index = jfcn < channels->num ? jfcn : jfcn - channels->num;
	if (index >= channels->num)
		return -EINVAL;

	if (jfcn > channels->num)
		cq = channels->c[index].rq->cq;
	else
		cq = channels->c[index].sq->cq;

	cq->event_cnt++;

	napi_schedule(&channels->c[index].napi);

	return 0;
}

static struct ubase_ctrlq_event_nb unic_ctrlq_events[] = {
	{
		.service_type = UBASE_CTRLQ_SER_TYPE_IP_ACL,
		.opcode = UBASE_CTRLQ_OPC_NOTIFY_IP,
		.crq_handler = unic_handle_notify_ip_event,
	}
};

static void unic_unregister_ctrlq_event(struct auxiliary_device *adev,
					u32 ctrlq_crq_event_num)
{
	u32 i;

	for (i = 0; i < ctrlq_crq_event_num; i++)
		ubase_ctrlq_unregister_crq_event(adev,
						 unic_ctrlq_events[i].service_type,
						 unic_ctrlq_events[i].opcode);
}

static int unic_register_ctrlq_event(struct auxiliary_device *adev)
{
	int ret;
	u32 i;

	for (i = 0; i < ARRAY_SIZE(unic_ctrlq_events); i++) {
		unic_ctrlq_events[i].back = adev;
		ret = ubase_ctrlq_register_crq_event(adev, &unic_ctrlq_events[i]);
		if (ret) {
			dev_err(adev->dev.parent,
				"failed to register ctrlq event[%u], ret = %d.\n",
				i, ret);
			unic_unregister_ctrlq_event(adev, i);
			return ret;
		}
	}

	return 0;
}

static struct ubase_crq_event_nb unic_crq_events[] = {
	{
		.opcode = UBASE_OPC_QUERY_LINK_STATUS,
		.crq_handler = unic_handle_link_status_event,
	},
};

static void unic_unregister_crq_event(struct auxiliary_device *adev,
				      u32 crq_event_num)
{
	u32 i;

	for (i = 0; i < crq_event_num; i++)
		ubase_unregister_crq_event(adev, unic_crq_events[i].opcode);
}

static int unic_register_crq_event(struct auxiliary_device *adev)
{
	int ret;
	u32 i;

	for (i = 0; i < ARRAY_SIZE(unic_crq_events); i++) {
		unic_crq_events[i].back = adev;

		ret = ubase_register_crq_event(adev, &unic_crq_events[i]);
		if (ret) {
			dev_err(adev->dev.parent,
				"failed to register crq event[%u], ret = %d.\n",
				i, ret);
			unic_unregister_crq_event(adev, i);
			return ret;
		}
	}

	return 0;
}

int unic_register_event(struct auxiliary_device *adev)
{
	int ret;

	ret = unic_register_crq_event(adev);
	if (ret)
		return ret;

	ret = unic_register_ctrlq_event(adev);
	if (ret)
		goto unregister_crq;

	return 0;

unregister_crq:
	unic_unregister_crq_event(adev, ARRAY_SIZE(unic_crq_events));
	return ret;
}

void unic_unregister_event(struct auxiliary_device *adev)
{
	unic_unregister_ctrlq_event(adev, ARRAY_SIZE(unic_ctrlq_events));
	unic_unregister_crq_event(adev, ARRAY_SIZE(unic_crq_events));
}

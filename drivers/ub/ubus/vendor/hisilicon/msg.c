// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) HiSilicon Technologies Co., Ltd. 2025. All rights reserved.
 */

#define pr_fmt(fmt) "ubus hisi msg: " fmt

#include "../../ubus.h"
#include "../../msg.h"
#include "hisi-msg.h"

struct hi_message_device {
	struct hi_msg_core hmc;
	struct message_device mdev;
};

#define to_hi_message_device(dev) \
	container_of(dev, struct hi_message_device, mdev)

static int hi_msg_queue_init(struct hi_message_device *hmd)
{
	struct hi_msg_core *hmc = &hmd->hmc;

	return hi_msg_core_init(hmc, MSGQ_USER_BUS_DRV);
}

static void hi_msg_queue_uninit(struct hi_message_device *hmd)
{
	hi_msg_core_uninit(&hmd->hmc);
}

static struct message_ops hi_message_ops = {};

int hi_msg_device_probe(struct ub_bus_controller *ubc)
{
	struct device *dev = &ubc->dev;
	struct hi_message_device *hmd;
	struct hi_msg_core *hmc;
	int ret;

	hmd = kzalloc(sizeof(*hmd), GFP_KERNEL);
	if (!hmd)
		return -ENOMEM;

	hmc = &hmd->hmc;
	hmc->dev = dev;
	hmc->q_addr = ubc->attr.queue_addr;
	hmc->q_size = HI_MSGQ_SIZE;

	ret = hi_msg_queue_init(hmd);
	if (ret) {
		dev_err(dev, "init message queue failed\n");
		goto queue_init_fail;
	}

	message_device_set_ops(&hmd->mdev, &hi_message_ops);
	message_device_set_fwnode(&hmd->mdev, dev->fwnode);

	ret = message_device_register(&hmd->mdev);
	if (ret) {
		dev_err(dev, "register message device failed\n");
		goto mdev_reg_fail;
	}

	ubc->mdev = &hmd->mdev;

	return 0;

mdev_reg_fail:
	hi_msg_queue_uninit(hmd);
queue_init_fail:
	kfree(hmd);
	return ret;
}

void hi_msg_device_remove(struct ub_bus_controller *ubc)
{
	struct hi_message_device *hmd = to_hi_message_device(ubc->mdev);

	ubc->mdev = NULL;
	message_device_unregister(&hmd->mdev);
	hi_msg_queue_uninit(hmd);
	kfree(hmd);
}

// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) HiSilicon Technologies Co., Ltd. 2025. All rights reserved.
 */

#define pr_fmt(fmt)	"ubus msg: " fmt

#include <linux/delay.h>
#include <linux/limits.h>
#include <linux/module.h>

#include "msg.h"

u8 err_to_msg_rsp(int err)
{
	if (err >= 0)
		return err;

	switch (err) {
	case -ENOMEM:
		return UB_MSG_RSP_EXEC_ENOMEM;
	case -EACCES:
		return UB_MSG_RSP_EXEC_EACCES;
	case -EFAULT:
		return UB_MSG_RSP_EXEC_EFAULT;
	case -EBUSY:
		return UB_MSG_RSP_EXEC_EBUSY;
	case -ENODEV:
		return UB_MSG_RSP_EXEC_ENODEV;
	case -EINVAL:
		return UB_MSG_RSP_EXEC_EINVAL;
	case -ENOEXEC:
		return UB_MSG_RSP_EXEC_ENOEXEC;
	default:
		return UB_MSG_RSP_UNKNOWN;
	}
}

static LIST_HEAD(message_device_list);
static DEFINE_SPINLOCK(message_device_lock);

int message_device_register(struct message_device *mdev)
{
	spin_lock(&message_device_lock);
	list_add_tail(&mdev->list, &message_device_list);
	spin_unlock(&message_device_lock);

	return 0;
}
EXPORT_SYMBOL_GPL(message_device_register);

void message_device_unregister(struct message_device *mdev)
{
	spin_lock(&message_device_lock);
	list_del(&mdev->list);
	spin_unlock(&message_device_lock);
}
EXPORT_SYMBOL_GPL(message_device_unregister);

static struct dev_message *dev_message_get(struct ub_entity *uent)
{
	struct dev_message *msg = uent->message;

	if (msg)
		return msg;

	msg = kzalloc(sizeof(*msg), GFP_KERNEL);
	if (!msg)
		return NULL;

	uent->message = msg;

	return msg;
}

static void dev_message_put(struct ub_entity *uent)
{
	kfree(uent->message);
	uent->message = NULL;
}

int message_probe_device(struct ub_entity *uent)
{
	const struct message_ops *ops = uent->ubc->mdev->ops;
	int ret;

	if (!dev_message_get(uent))
		return -ENOMEM;

	if (uent->message->mdev)
		return 0;

	if (ops->probe_dev) {
		ret = ops->probe_dev(uent);
		if (ret)
			goto err_probe;
	}

	uent->message->mdev = uent->ubc->mdev;

	return 0;

err_probe:
	dev_message_put(uent);
	return ret;
}

void message_remove_device(struct ub_entity *uent)
{
	const struct message_ops *ops = uent->ubc->mdev->ops;

	if (!uent->message)
		return;

	if (ops->remove_dev)
		ops->remove_dev(uent);
	dev_message_put(uent);
}

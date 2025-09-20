// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) HiSilicon Technologies Co., Ltd. 2025. All rights reserved.
 */

#define pr_fmt(fmt)	"ubus ioctl: " fmt

#include <uapi/ub/ubus/ubus.h>
#include <linux/cdev.h>

#include "ubus.h"

#define UBUS_MAX_DEVICES 1
#define UBUS_DEVICE_NAME "unified_bus"
#define UBUS_CLASS_NAME "unified_bus"

struct unified_bus_ctx {
	dev_t devno;
	struct cdev cdev;
	struct class *ub_class;
	struct device *dev;
};
static struct unified_bus_ctx ubus_ctx;

static int ubus_fops_open(struct inode *inode, struct file *filep)
{
	return 0;
}

static int ubus_fops_release(struct inode *inode, struct file *filep)
{
	return 0;
}

static long ubus_fops_unl_ioctl(struct file *filep,
				unsigned int cmd, unsigned long arg)
{
	switch (cmd) {
	case UBUS_IOCTL_GET_API_VERSION:
		return UBUS_API_VERSION;
	default:
		pr_err("ubus ioctl cmd %#x not support\n", cmd);
		return -ENOTTY;
	}
}

static const struct file_operations ubus_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = ubus_fops_unl_ioctl,
	.open = ubus_fops_open,
	.release = ubus_fops_release,
};

int ub_cdev_init(void)
{
	struct cdev *cdev = &ubus_ctx.cdev;
	struct device *dev;
	struct class *cls;
	dev_t devno;
	int ret;

	ret = alloc_chrdev_region(&devno, 0, UBUS_MAX_DEVICES, UBUS_DEVICE_NAME);
	if (ret)
		return ret;

	cdev_init(cdev, &ubus_fops);
	ret = cdev_add(cdev, devno, UBUS_MAX_DEVICES);
	if (ret)
		goto out_unregister;

	cls = class_create(UBUS_CLASS_NAME);
	if (IS_ERR(cls)) {
		ret = PTR_ERR(cls);
		goto out_del;
	}

	dev = device_create(cls, NULL, devno, NULL, UBUS_DEVICE_NAME);
	if (IS_ERR(dev)) {
		ret = PTR_ERR(dev);
		goto out_destroy;
	}

	ubus_ctx.ub_class = cls;
	ubus_ctx.dev = dev;
	ubus_ctx.devno = devno;
	return 0;

out_destroy:
	class_destroy(cls);
out_del:
	cdev_del(cdev);
out_unregister:
	unregister_chrdev_region(devno, UBUS_MAX_DEVICES);
	return ret;
}

void ub_cdev_uninit(void)
{
	device_destroy(ubus_ctx.ub_class, ubus_ctx.devno);
	class_destroy(ubus_ctx.ub_class);
	cdev_del(&ubus_ctx.cdev);
	unregister_chrdev_region(ubus_ctx.devno, UBUS_MAX_DEVICES);
}

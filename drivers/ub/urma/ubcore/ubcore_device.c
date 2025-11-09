// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2021-2025. All rights reserved.
 *
 * Description: ubcore device add and remove ops file
 * Author: Qian Guoxin
 * Create: 2021-08-03
 * Note:
 * History: 2021-08-03: create file
 */

#include <linux/netdevice.h>
#include <linux/version.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/device.h>
#include <linux/cdev.h>

#include "ubcore_log.h"
#include "ubcore_device.h"

#define UBCORE_MAX_MUE_NUM 16
#define UBCORE_DEVICE_NAME "ubcore"

struct ubcore_ctx {
	dev_t ubcore_devno;
	struct cdev ubcore_cdev;
	struct device *ubcore_dev;
};

static int ubcore_global_open(struct inode *i_node, struct file *filp)
{
	ubcore_log_info("open ubcore global file succeed.\n");
	return 0;
}

static long ubcore_global_ioctl(struct file *filp, unsigned int cmd,
				unsigned long arg)
{
	ubcore_log_err("bad ioctl command.\n");
	return -ENOIOCTLCMD;
}

static int ubcore_global_close(struct inode *i_node, struct file *filp)
{
	ubcore_log_info("closing ubcore global device.\n");
	return 0;
}

static const struct file_operations g_ubcore_global_ops = {
	.owner = THIS_MODULE,
	.open = ubcore_global_open,
	.release = ubcore_global_close,
	.unlocked_ioctl = ubcore_global_ioctl,
	.compat_ioctl = ubcore_global_ioctl,
};

static dev_t g_dynamic_mue_devnum;
static struct ubcore_ctx g_ubcore_ctx = { 0 };

static char *ubcore_devnode(const struct device *dev, umode_t *mode)

{
	if (mode)
		*mode = UBCORE_DEVNODE_MODE;

	return kasprintf(GFP_KERNEL, "ubcore/%s", dev_name(dev));
}

static struct class g_ubcore_class = { .name = "ubcore",
				       .devnode = ubcore_devnode,
				       .ns_type = &net_ns_type_operations,
				       .namespace = NULL };

int ubcore_class_register(void)
{
	int ret;

	// Allocate device numbers for MUE
	ret = alloc_chrdev_region(&g_dynamic_mue_devnum, 0, UBCORE_MAX_MUE_NUM,
				  UBCORE_DEVICE_NAME);
	if (ret != 0) {
		ubcore_log_err(
			"couldn't register dynamic device number for mue.\n");
		return ret;
	}

	ret = class_register(&g_ubcore_class);
	if (ret) {
		unregister_chrdev_region(g_dynamic_mue_devnum,
					 UBCORE_MAX_MUE_NUM);
		ubcore_log_err("couldn't create ubcore class\n");
	}
	return ret;
}

void ubcore_class_unregister(void)
{
	class_unregister(&g_ubcore_class);
	unregister_chrdev_region(g_dynamic_mue_devnum, UBCORE_MAX_MUE_NUM);
}

int ubcore_cdev_register(void)
{
	int ret;

	// If sysfs is created, return Success
	// Need to add mutex
	if (!IS_ERR_OR_NULL(g_ubcore_ctx.ubcore_dev))
		return 0;

	ret = alloc_chrdev_region(&g_ubcore_ctx.ubcore_devno, 0, 1,
				  UBCORE_DEVICE_NAME);
	if (ret != 0) {
		ubcore_log_err("alloc chrdev region failed, ret:%d.\n", ret);
		return ret;
	}

	cdev_init(&g_ubcore_ctx.ubcore_cdev, &g_ubcore_global_ops);
	g_ubcore_ctx.ubcore_cdev.owner = THIS_MODULE;

	ret = cdev_add(&g_ubcore_ctx.ubcore_cdev, g_ubcore_ctx.ubcore_devno, 1);
	if (ret != 0) {
		ubcore_log_err("chrdev add failed, ret:%d.\n", ret);
		goto unreg_cdev_region;
	}

	/* /dev/ubcore */
	g_ubcore_ctx.ubcore_dev = device_create(&g_ubcore_class, NULL,
						g_ubcore_ctx.ubcore_devno, NULL,
						UBCORE_DEVICE_NAME);
	if (IS_ERR(g_ubcore_ctx.ubcore_dev)) {
		ret = (int)PTR_ERR(g_ubcore_ctx.ubcore_dev);
		ubcore_log_err("couldn't create device %s, ret:%d.\n",
			       UBCORE_DEVICE_NAME, ret);
		g_ubcore_ctx.ubcore_dev = NULL;
		goto del_cdev;
	}
	ubcore_log_info("ubcore device created success.\n");
	return 0;

del_cdev:
	cdev_del(&g_ubcore_ctx.ubcore_cdev);
unreg_cdev_region:
	unregister_chrdev_region(g_ubcore_ctx.ubcore_devno, 1);
	return ret;
}

int ubcore_cdev_unregister(void)
{
	// If sysfs is not created, return Success
	// Need to add mutex
	if (IS_ERR_OR_NULL(g_ubcore_ctx.ubcore_dev))
		return 0;

	device_destroy(&g_ubcore_class, g_ubcore_ctx.ubcore_cdev.dev);
	cdev_del(&g_ubcore_ctx.ubcore_cdev);
	unregister_chrdev_region(g_ubcore_ctx.ubcore_devno, 1);
	ubcore_log_info("ubcore sysfs device destroyed success.\n");
	return 0;
}


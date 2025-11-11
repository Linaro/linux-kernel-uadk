// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2021-2025. All rights reserved.
 *
 * Description: ubcore kernel module
 * Author: Qian Guoxin
 * Create: 2021-08-03
 * Note:
 * History: 2021-08-03: create file
 */

#include <linux/module.h>

#include "ubcore_main.h"
#include "ubcore_log.h"
#include "ubcore_workqueue.h"
#include "ubcore_device.h"

static int __init ubcore_init(void)
{
	int ret;

	ret = ubcore_class_register();
	if (ret != 0)
		return ret;

	ret = ubcore_cdev_register();
	if (ret != 0)
		goto class_init;

	ret = ubcore_create_workqueues();
	if (ret != 0) {
		ubcore_log_err("Failed to create all the workqueues, ret = %d\n", ret);
		goto register_cdev;
	}

	ubcore_log_info("ubcore module init success.\n");
	return 0;

register_cdev:
	ubcore_cdev_unregister();
class_init:
	ubcore_class_unregister();
	return ret;
}

static void __exit ubcore_exit(void)
{
	ubcore_destroy_workqueues();
	ubcore_cdev_unregister();
	ubcore_class_unregister();
	ubcore_log_info("ubcore module exits.\n");
}

module_init(ubcore_init);
module_exit(ubcore_exit);

MODULE_DESCRIPTION("URMA memory semantic kernel module for direct remote memory access.");
MODULE_AUTHOR("huawei");
MODULE_LICENSE("GPL");

// SPDX-License-Identifier: GPL-2.0+
/* Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved. */

#define pr_fmt(fmt) "CDMA: " fmt
#define dev_fmt pr_fmt

#include <linux/module.h>

#include "cdma.h"
#include "cdma_dev.h"
#include "cdma_chardev.h"
#include <ub/ubase/ubase_comm_dev.h>
#include "cdma_cmd.h"

/* Enabling jfc_arm_mode will cause jfc to report cqe; otherwise, it will not. */
uint jfc_arm_mode;
module_param(jfc_arm_mode, uint, 0444);
MODULE_PARM_DESC(jfc_arm_mode,
		 "Set the ARM mode of the JFC, default: 0(0:Always ARM, others: NO ARM)");

struct class *cdma_cdev_class;

static int cdma_init_dev_info(struct auxiliary_device *auxdev, struct cdma_dev *cdev)
{
	int ret;

	/* query eu failure does not affect driver loading, as eu can be updated. */
	ret = cdma_ctrlq_query_eu(cdev);
	if (ret)
		dev_warn(&auxdev->dev, "query eu failed, ret = %d.\n", ret);

	return 0;
}

static int cdma_init_dev(struct auxiliary_device *auxdev)
{
	struct cdma_dev *cdev;
	int ret;

	dev_dbg(&auxdev->dev, "%s called, matched aux dev(%s.%u).\n",
		 __func__, auxdev->name, auxdev->id);

	cdev = cdma_create_dev(auxdev);
	if (!cdev)
		return -ENOMEM;

	ret = cdma_create_chardev(cdev);
	if (ret) {
		cdma_destroy_dev(cdev);
		return ret;
	}

	ret = cdma_init_dev_info(auxdev, cdev);
	if (ret) {
		cdma_destroy_chardev(cdev);
		cdma_destroy_dev(cdev);
		return ret;
	}

	return ret;
}

static void cdma_uninit_dev(struct auxiliary_device *auxdev)
{
	struct cdma_dev *cdev;

	dev_dbg(&auxdev->dev, "%s called, matched aux dev(%s.%u).\n",
		 __func__, auxdev->name, auxdev->id);

	cdev = dev_get_drvdata(&auxdev->dev);
	if (!cdev) {
		dev_err(&auxdev->dev, "get drvdata from ubase failed.\n");
		return;
	}

	cdma_destroy_chardev(cdev);
	cdma_destroy_dev(cdev);
}

static int cdma_probe(struct auxiliary_device *auxdev,
		      const struct auxiliary_device_id *auxdev_id)
{
	int ret;

	ret = cdma_init_dev(auxdev);
	if (ret)
		return ret;

	return 0;
}

static void cdma_remove(struct auxiliary_device *auxdev)
{
	cdma_uninit_dev(auxdev);
}

static const struct auxiliary_device_id cdma_id_table[] = {
	{
		.name = UBASE_ADEV_NAME ".cdma",
	},
	{}
};
MODULE_DEVICE_TABLE(auxiliary, cdma_id_table);

static struct auxiliary_driver cdma_driver = {
	.probe = cdma_probe,
	.remove = cdma_remove,
	.name = "cdma",
	.id_table = cdma_id_table,
};

static int __init cdma_init(void)
{
	int ret;

	cdma_cdev_class = class_create("cdma");
	if (IS_ERR(cdma_cdev_class)) {
		pr_err("create cdma class failed.\n");
		return PTR_ERR(cdma_cdev_class);
	}

	ret = auxiliary_driver_register(&cdma_driver);
	if (ret) {
		pr_err("auxiliary register failed.\n");
		goto free_class;
	}

	return 0;

free_class:
	class_destroy(cdma_cdev_class);

	return ret;
}

static void __exit cdma_exit(void)
{
	auxiliary_driver_unregister(&cdma_driver);
	class_destroy(cdma_cdev_class);
}

module_init(cdma_init);
module_exit(cdma_exit);
MODULE_LICENSE("GPL");
MODULE_VERSION("1.0");
MODULE_DESCRIPTION("Hisilicon UBus Crystal DMA Driver");

// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2012 Red Hat, Inc.  All rights reserved.
 * Author: Alex Williamson <alex.williamson@redhat.com>
 *
 * Derived from original vfio:
 * Copyright 2010 Cisco Systems, Inc.  All rights reserved.
 * Author: Tom Lyon, pugs@cisco.com
 *
 * Thanks to Alex Williamson and Tom Lyon for their original
 * vfio implementation.
 *
 * Copyright (c) HiSilicon Technologies Co., Ltd. 2025. All rights reserved.
 */

#define pr_fmt(fmt) "vfio ub: " fmt

#include <linux/module.h>
#include <linux/vfio.h>
#include <linux/types.h>

#include "vfio_ub_private.h"

static const struct vfio_device_ops vfio_ub_ops = {
	.name = "vfio-ub",
	.init = vfio_ub_core_init_dev,
	.release = vfio_ub_core_release_dev,
	.open_device = vfio_ub_core_open_device,
	.close_device = vfio_ub_core_close_device,
	.read = vfio_ub_core_read,
	.write = vfio_ub_core_write,
	.mmap = vfio_ub_core_mmap,
};

static int vfio_ub_probe(struct ub_entity *uent, const struct ub_device_id *id)
{
	struct vfio_ub_core_device *vdev;
	int ret;

	vdev = vfio_alloc_device(vfio_ub_core_device, vdev, &uent->dev, &vfio_ub_ops);
	if (IS_ERR(vdev))
		return PTR_ERR(vdev);

	dev_set_drvdata(&uent->dev, vdev);
	ret = vfio_ub_core_register_device(vdev);
	if (ret)
		goto out_put_vdev;

	ub_dbg(uent, "vfio-ub driver has matched\n");
	return 0;

out_put_vdev:
	dev_set_drvdata(&uent->dev, NULL);
	vfio_put_device(&vdev->vdev);
	return ret;
}

static void vfio_ub_remove(struct ub_entity *uent)
{
	struct vfio_ub_core_device *vdev =
		(struct vfio_ub_core_device *)dev_get_drvdata(&uent->dev);

	vfio_ub_core_disable_all(vdev);
	vfio_ub_core_unregister_device(vdev);
	dev_set_drvdata(&uent->dev, NULL);
	vfio_put_device(&vdev->vdev);

	ub_dbg(uent, "vfio-ub driver has removed\n");
}

static const struct ub_device_id vfio_ub_table[] = {
	{ UB_DRIVER_OVERRIDE_ENTITY_VFIO(UB_ANY_ID, UB_ANY_ID) }, /* match all by default */
	{}
};
MODULE_DEVICE_TABLE(ub, vfio_ub_table);

static struct ub_driver vfio_ub_driver = {
	.name = "vfio-ub",
	.id_table = vfio_ub_table,
	.probe = vfio_ub_probe,
	.remove = vfio_ub_remove,
	.driver_managed_dma = true,
};

static int __init vfio_ub_init(void)
{
	int ret;

	ret = vfio_ub_init_perm_bits();
	if (ret)
		return ret;

	ret = ub_register_driver(&vfio_ub_driver);
	if (ret)
		goto out_driver;

	return 0;

out_driver:
	vfio_ub_uninit_perm_bits();
	return ret;
}

static void __exit vfio_ub_exit(void)
{
	ub_unregister_driver(&vfio_ub_driver);
	vfio_ub_uninit_perm_bits();
}

module_init(vfio_ub_init);
module_exit(vfio_ub_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("VFIO UB - User Level meta-driver");
MODULE_IMPORT_NS(UB_UBUS);

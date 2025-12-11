// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) HiSilicon Technologies Co., Ltd. 2025. All rights reserved.
 */

#define pr_fmt(fmt) "ubus service: " fmt

#include "../ubus.h"
#include "../ubus_driver.h"
#include "service.h"

static int ub_service_probe(struct device *dev)
{
	struct ub_service_device *sdev;
	struct ub_service_driver *sdrv;
	int status;

	if (!dev || !dev->driver)
		return -ENODEV;

	sdrv = to_ub_service_driver(dev->driver);
	if (!sdrv || !sdrv->probe)
		return -ENODEV;

	sdev = to_ub_service_device(dev);
	status = sdrv->probe(sdev);
	if (status)
		return status;

	get_device(dev);
	return 0;
}

static int ub_service_remove(struct device *dev)
{
	struct ub_service_device *sdev;
	struct ub_service_driver *sdrv;

	if (!dev || !dev->driver)
		return 0;

	sdev = to_ub_service_device(dev);
	sdrv = to_ub_service_driver(dev->driver);
	if (sdrv && sdrv->remove) {
		sdrv->remove(sdev);
		put_device(dev);
	}

	return 0;
}

static void ub_service_shutdown(struct device *dev)
{
}

int ub_service_driver_register(struct ub_service_driver *drv)
{
	drv->driver.name = drv->name;
	drv->driver.bus = &ub_service_bus_type;
	drv->driver.probe = ub_service_probe;
	drv->driver.remove = ub_service_remove;
	drv->driver.shutdown = ub_service_shutdown;

	return driver_register(&drv->driver);
}

void ub_service_driver_unregister(struct ub_service_driver *drv)
{
	driver_unregister(&drv->driver);
}

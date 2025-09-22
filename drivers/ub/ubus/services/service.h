/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) HiSilicon Technologies Co., Ltd. 2025. All rights reserved.
 */
#ifndef __SERVICES_SERVICE_H__
#define __SERVICES_SERVICE_H__

struct ub_service_device {
	int irq;
	struct ub_entity *uent;
	u32 service;
	void *priv_data;
	struct device device;
};

struct ub_service_driver {
	const char *name;
	int (*probe)(struct ub_service_device *dev);
	void (*remove)(struct ub_service_device *dev);

	u32 service;

	struct device_driver driver;
};

#define to_ub_service_device(d) \
	container_of(d, struct ub_service_device, device)
#define to_ub_service_driver(d) \
	container_of(d, struct ub_service_driver, driver)

int ub_service_driver_register(struct ub_service_driver *drv);
void ub_service_driver_unregister(struct ub_service_driver *drv);

#endif /* __SERVICES_SERVICE_H__ */

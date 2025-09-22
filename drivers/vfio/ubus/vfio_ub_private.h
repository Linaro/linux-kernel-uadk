/* SPDX-License-Identifier: GPL-2.0+ */
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
#ifndef __VFIO_UB_PRIVATE_H__
#define __VFIO_UB_PRIVATE_H__

#include <uapi/ub/ubus/ubus_regs.h>
#include <linux/irqbypass.h>
#include <ub/ubus/ubus.h>

struct vfio_ub_core_device {
	struct vfio_device vdev;
	struct ub_entity *uent;
	int num_regions; /* vfio-ub additional regions */
	int num_ext_irqs; /* vfio-ub additional extended irqs */
	int num_vendor_regions; /* vfio-ub additional vendor-defined regions */
	int num_vendor_irqs; /* vfio-ub additional vendor-defined irqs */
	bool reset_works; /* vfio-ub support reset flag */
	u64 usi_vector_offset;
	u32 usi_vector_size;
	u64 usi_addr_offset;
	u32 usi_addr_size;
	struct eventfd_ctx *req_trigger;
	struct mutex igate;
};

int vfio_ub_core_register_device(struct vfio_ub_core_device *vdev);
void vfio_ub_core_unregister_device(struct vfio_ub_core_device *vdev);
int vfio_ub_core_open_device(struct vfio_device *core_vdev);
void vfio_ub_core_close_device(struct vfio_device *core_vdev);
int vfio_ub_core_init_dev(struct vfio_device *core_vdev);
void vfio_ub_core_release_dev(struct vfio_device *core_dev);
void vfio_ub_core_disable_all(struct vfio_ub_core_device *vdev);

#endif /* __VFIO_UB_PRIVATE_H__ */

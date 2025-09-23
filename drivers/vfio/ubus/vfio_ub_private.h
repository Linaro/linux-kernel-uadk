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

#define UB_CFG0_MAX_CAP 255
#define UB_CFG1_MAX_CAP 511 /* cfg1 cap start from 256 */

struct vfio_ub_core_device;
struct perm_bits {
	u8 *virt;
	u8 *write;
	u8 *ent0eo;
	u8 *exist;
	int (*readfn)(struct vfio_ub_core_device *vdev, u64 pos, int count,
		      __le32 *val);
	int (*writefn)(struct vfio_ub_core_device *vdev, u64 pos, int count,
		       __le32 val);
};

struct vfio_ub_slice {
	u8 *cfg;
	u32 cap_id;
	int used_size;
};

struct vfio_ub_config {
	struct vfio_ub_slice *slice;
	int nums;
	u8 final_slice_supported[UB_CFG1_MAX_CAP + 1]; /* do not incude port basic && port cap */
	int map[UB_CFG1_MAX_CAP + 1];
};

struct vfio_ub_core_device {
	struct vfio_device vdev;
	struct ub_entity *uent;
	struct vfio_ub_config vconfig;
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

#define VFIO_UB_OFFSET_SHIFT 40
#define VFIO_UB_OFFSET_MASK (((u64)(1) << VFIO_UB_OFFSET_SHIFT) - 1)

#define BYTE_SIZE 1
#define WORD_SIZE 2
#define DWORD_SIZE 4
#define DWORD_BITS 32
#define BYTE_BITS 8

int vfio_ub_config_init(struct vfio_ub_core_device *vdev);
void vfio_ub_config_uninit(struct vfio_ub_core_device *vdev);
ssize_t vfio_ub_config_rw(struct vfio_ub_core_device *vdev, char __user *buf,
			  size_t count, loff_t *ppos, bool iswrite);
int vfio_ub_core_register_device(struct vfio_ub_core_device *vdev);
void vfio_ub_core_unregister_device(struct vfio_ub_core_device *vdev);
int vfio_ub_core_open_device(struct vfio_device *core_vdev);
void vfio_ub_core_close_device(struct vfio_device *core_vdev);
int vfio_ub_core_init_dev(struct vfio_device *core_vdev);
void vfio_ub_core_release_dev(struct vfio_device *core_dev);
void vfio_ub_core_disable_all(struct vfio_ub_core_device *vdev);

#endif /* __VFIO_UB_PRIVATE_H__ */

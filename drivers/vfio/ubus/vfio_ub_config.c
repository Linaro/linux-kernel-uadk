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

#include <linux/vfio.h>
#include <linux/uaccess.h>

#include "vfio_ub_private.h"

#define NO_ENTITY0_EO 0
#define ENTITY0_EO 0xFFFFFFFFU

/* Now, this attr control just support at byte granularity */
enum {
	FUNC_EXIST = 0x00000000, /* idev and ub device have this register */
	IDEV_NO_EXIST = 0x01010101, /* idev do not has this register */
	UDEV_NO_EXIST = 0x02020202, /* ub device do not has have this register */
	FUNC_NO_EXIST = 0x03030303, /* idev and ub device do not have this register */
};

struct attrs {
	__le32 ent0eo;
	__le32 exist;
};

#define CFG_MAP_INVALID (-1)

#define UB_PORT_CAP_GAP 0x100
#define UB_PORT0_BASIC_CAP 0x200

#define UB_CFG_POS_TO_CAP(pos) ((u64)(pos) >> 10)
#define UB_CFG_CAP_TO_POS(cap) ((u32)(cap) << 10)

static const u8 zero_array[UB_SLICE_SZ] = {};

static int vfio_ub_do_user_config_read(struct ub_entity *uent, u64 pos,
				       __le32 *val, int count)
{
	int ret = -EINVAL;
	u32 tmp_val = 0;
	u8 tmp_byte;
	u16 tmp_word;

	switch (count) {
	case BYTE_SIZE:
		ret = ub_cfg_read_byte(uent, pos, &tmp_byte);
		tmp_val = tmp_byte;
		break;
	case WORD_SIZE:
		ret = ub_cfg_read_word(uent, pos, &tmp_word);
		tmp_val = tmp_word;
		break;
	case DWORD_SIZE:
		ret = ub_cfg_read_dword(uent, pos, &tmp_val);
		break;
	default:
		break;
	}

	*val = cpu_to_le32(tmp_val);

	return ret;
}

static bool vfio_ub_judge_type(struct ub_entity *uent, u8 exist)
{
	return ((((exist & IDEV_NO_EXIST) == 0) && (uent_type(uent) == UB_TYPE_ICONTROLLER &&
		uent_base_code(uent) != UB_BASE_CODE_BUS_CONTROLLER)) ||
		(((exist & UDEV_NO_EXIST) == 0) && (uent_type(uent) == UB_TYPE_CONTROLLER &&
		uent_base_code(uent) != UB_BASE_CODE_BUS_CONTROLLER)));
}

static bool vfio_ub_judge_reg_read(struct ub_entity *uent, u8 ent0eo, u8 exist)
{
	if (vfio_ub_judge_type(uent, exist)) {
		if (((ent0eo == (u8)ENTITY0_EO) && (uent->entity_idx == 0)) ||
		    (ent0eo == NO_ENTITY0_EO)) {
			return true;
		}
	}

	return false;
}

static int vfio_ub_get_split_cfg_val(struct ub_entity *uent, u64 pos,
				     struct attrs *attr, __le32 *val, u32 count)
{
	u32 tmp_val = 0;
	u8 byte;
	u8 ent0eo_byte;
	u8 exist_byte;
	int ret;

	for (u32 i = 0; i < count; i++) {
		ent0eo_byte = (attr->ent0eo >> i * BITS_PER_BYTE) & 0xff;
		exist_byte = (attr->exist >> i * BITS_PER_BYTE) & 0xff;
		if (vfio_ub_judge_reg_read(uent, ent0eo_byte, exist_byte)) {
			ret = ub_cfg_read_byte(uent, pos + i, &byte);
			if (!ret)
				tmp_val = tmp_val | (((u32)byte) << i * BITS_PER_BYTE);
			else
				return ret;
		}
	}

	*val = tmp_val;

	return 0;
}

static int vfio_ub_default_config_read(struct vfio_ub_core_device *vdev,
				       u64 pos, int count, __le32 *val);

static struct perm_bits cap_perms[UB_CFG1_MAX_CAP + 1] = {};

static struct perm_bits port_basic_perms = {.readfn = vfio_ub_default_config_read };

/* when call this function, cap_id is always exist */
static struct perm_bits *vfio_ub_find_cap_perm(u32 cap_id)
{
	if (cap_id <= UB_CFG1_MAX_CAP)
		return &cap_perms[cap_id];

	return &port_basic_perms;
}

static int vfio_ub_user_config_read(struct ub_entity *uent, u64 pos,
				    __le32 *val, int count)
{
	struct perm_bits *perm;
	struct attrs attr = {};
	u32 offset;
	u32 cap_id;

	cap_id = UB_CFG_POS_TO_CAP(pos);
	perm = vfio_ub_find_cap_perm(cap_id);
	offset = pos - UB_CFG_CAP_TO_POS(cap_id);

	memcpy(&attr.ent0eo, perm->ent0eo + offset, count);
	memcpy(&attr.exist, perm->exist + offset, count);

	/* quick processing for most situation */
	if (likely(attr.exist == FUNC_EXIST && attr.ent0eo == NO_ENTITY0_EO))
		return vfio_ub_do_user_config_read(uent, pos, val, count);

	/* read cfg at byte unit */
	return vfio_ub_get_split_cfg_val(uent, pos, &attr, val, count);
}

static u8 *vfio_ub_find_cfg_buf(struct vfio_ub_core_device *vdev, u32 cap_id)
{
	int i;
	int num;

	if (cap_id <= UB_CFG1_MAX_CAP) {
		num = vdev->vconfig.map[cap_id];
		if (num != CFG_MAP_INVALID)
			return vdev->vconfig.slice[num].cfg;
	} else {
		for (i = 0; i < vdev->vconfig.nums; i++) {
			if (vdev->vconfig.slice[i].cap_id == cap_id)
				return vdev->vconfig.slice[i].cfg;
		}
	}

	return NULL;
}

static int vfio_ub_default_config_read(struct vfio_ub_core_device *vdev,
				       u64 pos, int count, __le32 *val)
{
	struct perm_bits *perm;
	__le32 virt = 0;
	u8 *buf;
	u32 cap_id;
	u32 offset;

	cap_id = UB_CFG_POS_TO_CAP(pos);
	offset = pos - UB_CFG_CAP_TO_POS(cap_id);
	perm = vfio_ub_find_cap_perm(cap_id);
	buf = vfio_ub_find_cfg_buf(vdev, cap_id);
	if (buf == NULL)
		return -EINVAL;

	memcpy(val, buf + offset, count);
	memcpy(&virt, perm->virt + offset, count);

	if (cpu_to_le32(~0U >> (DWORD_BITS - (count * BYTE_BITS))) != virt) {
		struct ub_entity *uent = vdev->uent;
		__le32 phys_val = 0;
		int ret;

		ret = vfio_ub_user_config_read(uent, pos, &phys_val, count);
		if (ret)
			return ret;

		*val = (phys_val & ~virt) | (*val & virt);
	}

	return count;
}

static int vfio_ub_find_valid_cap(struct vfio_ub_core_device *vdev, u32 cap_id)
{
	int i;

	if (cap_id <= UB_CFG1_MAX_CAP) {
		if (vdev->vconfig.map[cap_id] != CFG_MAP_INVALID)
			return 0;
	} else {
		for (i = 0; i < vdev->vconfig.nums; i++) {
			if (vdev->vconfig.slice[i].cap_id == cap_id)
				return 0;
		}
	}

	return -ENOENT;
}

static int vfio_ub_find_cap_used_size(struct vfio_ub_core_device *vdev, u32 cap_id)
{
	int i;
	int num;

	if (cap_id <= UB_CFG1_MAX_CAP) {
		num = vdev->vconfig.map[cap_id];
		return vdev->vconfig.slice[num].used_size;
	}

	for (i = 0; i < vdev->vconfig.nums; i++) {
		if (vdev->vconfig.slice[i].cap_id == cap_id)
			return vdev->vconfig.slice[i].used_size;
	}

	return -ENOENT;
}

static ssize_t vfio_ub_config_rw_used_zone(struct vfio_ub_core_device *vdev,
					   char __user *buf, size_t count,
					   loff_t *ppos, bool iswrite)
{
	size_t cap_start_pos;
	struct perm_bits *perm;
	size_t left;
	u32 cap_id;
	__le32 val = 0;
	int used_size;
	int ret;

	cap_id = UB_CFG_POS_TO_CAP(*ppos);
	cap_start_pos = UB_CFG_CAP_TO_POS(cap_id);
	used_size = vfio_ub_find_cap_used_size(vdev, cap_id);

	left = cap_start_pos + used_size - *ppos;

	perm = vfio_ub_find_cap_perm(cap_id);

	count = min(count, left);
	if (count >= DWORD_SIZE && !(*ppos % DWORD_SIZE))
		count = DWORD_SIZE;
	else if (count >= WORD_SIZE && !(*ppos % WORD_SIZE))
		count = WORD_SIZE;
	else
		count = BYTE_SIZE;

	ret = count;

	if (iswrite) {
		if (!perm->writefn)
			return ret;

		if (copy_from_user(&val, buf, count))
			return -EFAULT;

		ret = perm->writefn(vdev, *ppos, count, val);
	} else {
		if (perm->readfn) {
			ret = perm->readfn(vdev, *ppos, count, &val);
			if (ret < 0)
				return ret;
		}

		if (copy_to_user(buf, &val, count))
			return -EFAULT;
	}

	return ret;
}

static ssize_t vfio_ub_config_rw_unused_zone(struct vfio_ub_core_device *vdev,
					     char __user *buf, size_t count,
					     loff_t *ppos, bool iswrite)
{
	size_t cap_start_pos;
	size_t left;
	u32 cap_id;

	cap_id = UB_CFG_POS_TO_CAP(*ppos);
	cap_start_pos = UB_CFG_CAP_TO_POS(cap_id);
	left = cap_start_pos + UB_SLICE_SZ - *ppos;

	count = min(count, left);
	if (iswrite)
		return count;

	/* read unused zone just return zero */
	if (copy_to_user(buf, zero_array, count))
		return -EFAULT;

	return count;
}

static ssize_t vfio_ub_config_do_rw(struct vfio_ub_core_device *vdev,
				    char __user *buf, size_t count,
				    loff_t *ppos, bool iswrite)
{
	u32 cap_id;
	ssize_t ret;
	size_t cap_start_pos;
	int used_size;

	if (*ppos < 0 || *ppos >= UB_ROUTE_TABLE_SLICE_START ||
	    *ppos + count > UB_ROUTE_TABLE_SLICE_START ||
	    count > UB_SLICE_SZ)
		return -EFAULT;

	cap_id = UB_CFG_POS_TO_CAP(*ppos);

	ret = vfio_ub_find_valid_cap(vdev, cap_id);
	if (ret < 0) {
		ret = vfio_ub_config_rw_unused_zone(vdev, buf,
						      count, ppos, iswrite);
	} else {
		cap_start_pos = UB_CFG_CAP_TO_POS(cap_id);
		used_size = vfio_ub_find_cap_used_size(vdev, cap_id);
		if (used_size < 0)
			return -EINVAL;

		if (*ppos < cap_start_pos + used_size) {
			ret = vfio_ub_config_rw_used_zone(vdev, buf,
							  count, ppos, iswrite);
		} else {
			ret = vfio_ub_config_rw_unused_zone(vdev, buf,
							    count, ppos, iswrite);
		}
	}

	return ret;
}

ssize_t vfio_ub_config_rw(struct vfio_ub_core_device *vdev, char __user *buf,
			  size_t count, loff_t *ppos, bool iswrite)
{
	size_t done = 0;
	int ret = 0;
	loff_t pos = *ppos;

	pos &= VFIO_UB_OFFSET_MASK;

	while (count) {
		ret = vfio_ub_config_do_rw(vdev, buf, count, &pos, iswrite);
		if (ret < 0)
			return ret;

		buf += ret;
		pos += ret;
		count -= ret;
		done += ret;
	}

	*ppos += done;

	return done;
}

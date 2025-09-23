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
#include <linux/slab.h>
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

static u8 vfio_slice_supported[UB_CFG1_MAX_CAP + 1];

static int vfio_ub_fill_slice_vconfig(struct vfio_ub_core_device *vdev,
				      int num, u32 pos, u32 size)
{
	__le32 *dwordp;
	u32 val = 0;
	int ret;
	u32 cap_id;
	u32 start = 0;

	cap_id = UB_CFG_POS_TO_CAP(pos);

	for (u32 i = 0; i < size; i += DWORD_SIZE) {
		ret = vfio_ub_user_config_read(vdev->uent, pos + i, &val, DWORD_SIZE);
		if (ret) {
			ub_info(vdev->uent, "read cfgspace pos[%#x] failed, used default val 0\n",
				pos + i);
			val = 0;
		}
		dwordp = (__le32 *)(&vdev->vconfig.slice[num].cfg[i + start]);
		*dwordp = cpu_to_le32(val);
	}

	/* vfio-ub support no port caps for userspace */
	if (cap_id >= UB_PORT0_BASIC_CAP)
		memset(&vdev->vconfig.slice[num].cfg[UB_CFG0_PORT_BITMAP], 0, UB_CFG_BITMAP_SIZE);

	return 0;
}

static struct ub_entity *ub_get_primary_ent(struct ub_entity *uent)
{
	if (uent->entity_idx == 0)
		return uent;

	return uent->is_mue ? uent->pue : uent->pue->pue;
}

static int vfio_ub_init_slice_config_info(struct vfio_ub_core_device *vdev,
					  u32 cap_id)
{
	struct vfio_ub_slice *pslice, *realloc_slice, *cur_slice;
	struct ub_entity *primary_dev;
	int used_size;
	u32 pos;
	int ret;
	u32 val;

	pos = UB_CFG_CAP_TO_POS(cap_id);
	if (cap_id != UB_PORT0_BASIC_CAP) {
		ret = ub_cfg_read_dword(vdev->uent, pos, &val);
	} else {
		primary_dev = ub_get_primary_ent(vdev->uent);
		ret = ub_cfg_read_dword(primary_dev, pos, &val);
	}

	if (ret) {
		ub_err(vdev->uent, "get cap[%#x] slice header failed\n", cap_id);
		return ret;
	}
	used_size = (val >> SZ_4) << SZ_2;

	if (used_size == 0 || used_size > UB_SLICE_SZ) {
		ub_err(vdev->uent, "invalid cap_id[%#x] used_size[%#x]\n", cap_id, used_size);
		return -EINVAL;
	}

	pslice = vdev->vconfig.slice;
	realloc_slice = (struct vfio_ub_slice *)krealloc(vdev->vconfig.slice,
				(vdev->vconfig.nums + 1) *
				sizeof(struct vfio_ub_slice), GFP_KERNEL);
	if (realloc_slice == NULL)
		return -ENOMEM;

	vdev->vconfig.slice = realloc_slice;

	cur_slice = vdev->vconfig.slice + vdev->vconfig.nums;
	cur_slice->cfg = kzalloc(used_size, GFP_KERNEL);
	if (cur_slice->cfg == NULL) {
		if (pslice == NULL) {
			kfree(vdev->vconfig.slice);
			vdev->vconfig.slice = NULL;
		}
		return -ENOMEM;
	}

	cur_slice->used_size = used_size;
	cur_slice->cap_id = cap_id;
	ret = vfio_ub_fill_slice_vconfig(vdev, vdev->vconfig.nums, pos, used_size);
	if (ret) {
		kfree(cur_slice->cfg);
		if (pslice == NULL) {
			kfree(vdev->vconfig.slice);
			vdev->vconfig.slice = NULL;
		}
		return ret;
	}

	if (cap_id <= UB_CFG1_MAX_CAP)
		vdev->vconfig.map[cap_id] = vdev->vconfig.nums;
	vdev->vconfig.nums++;

	return 0;
}

static int vfio_ub_find_cfg_cap(struct vfio_ub_core_device *vdev, u32 cap_id)
{
	int idx;

	for (idx = 0; idx < vdev->vconfig.nums; idx++) {
		if (vdev->vconfig.slice[idx].cap_id == cap_id)
			return idx;
	}

	return CFG_MAP_INVALID;
}

static void vfio_ub_uninit_slice_config_info(struct vfio_ub_core_device *vdev,
					     u32 cap_id)
{
	u32 tmp_cap;
	int num;

	if (cap_id <= UB_CFG1_MAX_CAP) {
		num = vdev->vconfig.map[cap_id];
		vdev->vconfig.map[cap_id] = CFG_MAP_INVALID;
	} else {
		num = vfio_ub_find_cfg_cap(vdev, cap_id);
	}

	if (num == CFG_MAP_INVALID)
		return;

	kfree(vdev->vconfig.slice[num].cfg);
	for (int i = num; i < vdev->vconfig.nums - 1; i++) {
		memcpy(vdev->vconfig.slice + i, vdev->vconfig.slice + i + 1,
		       sizeof(struct vfio_ub_slice));
		tmp_cap = vdev->vconfig.slice[i].cap_id;
		if (tmp_cap <= UB_CFG1_MAX_CAP)
			vdev->vconfig.map[tmp_cap] = i;
	}

	vdev->vconfig.nums--;
}

static int vfio_ub_init_cfg0_basic_info(struct vfio_ub_core_device *vdev)
{
	u8 *cfg0_bitmap_buf;
	int cfg0_basic_idx;
	int i, j, cap_idx = 0;
	int ret;

	ret = vfio_ub_init_slice_config_info(vdev, UB_CFG0_BASIC_CAP);
	if (ret)
		return ret;

	cfg0_basic_idx = vdev->vconfig.map[UB_CFG0_BASIC_CAP];
	cfg0_bitmap_buf = vdev->vconfig.slice[cfg0_basic_idx].cfg
				+ UB_CFG0_CAP_BITMAP;

	for (i = 0; i < UB_CFG_BITMAP_SIZE; i++) {
		for (j = 0; j < BITS_PER_BYTE; j++) {
			if (((cfg0_bitmap_buf[i] >> j) & 1) &&
			    vfio_slice_supported[cap_idx]) {
				vdev->vconfig.final_slice_supported[cap_idx] = 1;
			} else if (((cfg0_bitmap_buf[i] >> j) & 1) &&
				   vfio_slice_supported[cap_idx] == 0) {
				cfg0_bitmap_buf[i] = cfg0_bitmap_buf[i] &
						     ~(1 << j);
			}
			cap_idx++;
		}
	}

	return 0;
}

static void vfio_ub_uninit_cfg0_basic_info(struct vfio_ub_core_device *vdev)
{
	u32 cap_id;

	for (cap_id = 0; cap_id <= UB_CFG0_MAX_CAP; cap_id++) {
		if (vdev->vconfig.final_slice_supported[cap_id] == 1)
			vdev->vconfig.final_slice_supported[cap_id] = 0;
	}

	vfio_ub_uninit_slice_config_info(vdev, UB_CFG0_BASIC_CAP);
}

static int vfio_ub_init_cfg0_cap_info(struct vfio_ub_core_device *vdev)
{
	int cap_id;
	int ret;

	for (cap_id = 1; cap_id <= UB_CFG0_MAX_CAP; cap_id++) {
		if (vdev->vconfig.final_slice_supported[cap_id]) {
			ret = vfio_ub_init_slice_config_info(vdev, (u32)cap_id);
			if (ret)
				goto cfg0_cap_init_err;
		}
	}

	return 0;

cfg0_cap_init_err:
	for (cap_id = cap_id - 1; cap_id >= 1; cap_id--)
		vfio_ub_uninit_slice_config_info(vdev, cap_id);
	return ret;
}

static void vfio_ub_uninit_cfg0_cap_info(struct vfio_ub_core_device *vdev)
{
	u32 cap_id;

	for (cap_id = 1; cap_id <= UB_CFG0_MAX_CAP; cap_id++) {
		if (vdev->vconfig.final_slice_supported[cap_id])
			vfio_ub_uninit_slice_config_info(vdev, cap_id);
	}
}

static int vfio_ub_init_cfg1_basic_info(struct vfio_ub_core_device *vdev)
{
	u32 cap_idx = UB_CFG1_BASIC_CAP;
	u8 *cfg1_bitmap_buf;
	int cfg1_basic_idx;
	int i, j;
	int ret;

	ret = vfio_ub_init_slice_config_info(vdev, UB_CFG1_BASIC_CAP);
	if (ret)
		return ret;

	cfg1_basic_idx = vdev->vconfig.map[UB_CFG1_BASIC_CAP];
	cfg1_bitmap_buf = vdev->vconfig.slice[cfg1_basic_idx].cfg +
			  UB_CFG1_CAP_BITMAP - UB_CFG1_BASIC;

	for (i = 0; i < UB_CFG_BITMAP_SIZE; i++) {
		for (j = 0; j < BITS_PER_BYTE; j++) {
			if (((cfg1_bitmap_buf[i] >> j) & 1) &&
			    vfio_slice_supported[cap_idx]) {
				vdev->vconfig.final_slice_supported[cap_idx] = 1;
			} else if (((cfg1_bitmap_buf[i] >> j) & 1) &&
				   vfio_slice_supported[cap_idx] == 0) {
				cfg1_bitmap_buf[i] = cfg1_bitmap_buf[i] &
						     ~(1 << j);
			}
			cap_idx++;
		}
	}

	return 0;
}

static void vfio_ub_uninit_cfg1_basic_info(struct vfio_ub_core_device *vdev)
{
	u32 cap_id;

	for (cap_id = UB_DECODER_CAP; cap_id <= UB_CFG1_MAX_CAP; cap_id++) {
		if (vdev->vconfig.final_slice_supported[cap_id] == 1)
			vdev->vconfig.final_slice_supported[cap_id] = 0;
	}

	vfio_ub_uninit_slice_config_info(vdev, UB_CFG1_BASIC_CAP);
}

static int vfio_ub_init_cfg1_cap_info(struct vfio_ub_core_device *vdev)
{
	u32 cap_id;
	int ret;

	for (cap_id = UB_DECODER_CAP; cap_id <= UB_CFG1_MAX_CAP; cap_id++) {
		if (vdev->vconfig.final_slice_supported[cap_id]) {
			ret = vfio_ub_init_slice_config_info(vdev, cap_id);
			if (ret)
				goto cfg1_cap_init_err;
		}
	}

	return 0;

cfg1_cap_init_err:
	for (cap_id = cap_id - 1; cap_id >= UB_DECODER_CAP; cap_id--)
		vfio_ub_uninit_slice_config_info(vdev, cap_id);
	return ret;
}

static void vfio_ub_uninit_cfg1_cap_info(struct vfio_ub_core_device *vdev)
{
	u32 cap_id;

	for (cap_id = UB_DECODER_CAP; cap_id <= UB_CFG1_MAX_CAP; cap_id++) {
		if (vdev->vconfig.final_slice_supported[cap_id])
			vfio_ub_uninit_slice_config_info(vdev, cap_id);
	}
}

static void vfio_ub_init_port_cap_bitmap(struct vfio_ub_core_device *vdev, int idx)
{
	/* now we do not support any port cap */
	memset(vdev->vconfig.slice[idx].cfg + UB_CFG0_PORT_BITMAP,
	       0, UB_CFG_BITMAP_SIZE);
}

static int vfio_ub_init_port_basic_info(struct vfio_ub_core_device *vdev)
{
	int cfg0_basic_idx = vdev->vconfig.map[UB_CFG0_BASIC_CAP];
	u16 total_port_nums;
	__le16 *wordp;
	int ret;
	int idx;

	wordp = (__le16 *)(vdev->vconfig.slice[cfg0_basic_idx].cfg +
			   UB_TOTAL_NUMBER_PORT);
	total_port_nums = le16_to_cpu(*wordp);

	for (idx = 0; idx < total_port_nums; idx++) {
		ret = vfio_ub_init_slice_config_info(vdev, UB_PORT0_BASIC_CAP +
						     idx * UB_PORT_CAP_GAP);
		if (ret)
			goto port_basic_init_err;
		vfio_ub_init_port_cap_bitmap(vdev, vdev->vconfig.nums - 1);
	}

	return 0;

port_basic_init_err:
	for (idx = idx - 1; idx >= 0; idx--)
		vfio_ub_uninit_slice_config_info(vdev, UB_PORT0_BASIC_CAP +
						 idx * UB_PORT_CAP_GAP);
	return ret;
}

static void vfio_ub_uninit_port_basic_info(struct vfio_ub_core_device *vdev)
{
	int cfg0_basic_idx = vdev->vconfig.map[UB_CFG0_BASIC_CAP];
	u16 total_port_nums;
	u16 idx;
	__le16 *wordp;

	wordp = (__le16 *)(vdev->vconfig.slice[cfg0_basic_idx].cfg +
			  UB_TOTAL_NUMBER_PORT);
	total_port_nums = le16_to_cpu(*wordp);
	for (idx = 0; idx < total_port_nums; idx++)
		vfio_ub_uninit_slice_config_info(vdev, UB_PORT0_BASIC_CAP +
						 idx * UB_PORT_CAP_GAP);
}

int vfio_ub_config_init(struct vfio_ub_core_device *vdev)
{
	int ret;

	memset(vdev->vconfig.map, CFG_MAP_INVALID,
	       (UB_CFG1_MAX_CAP + 1) * sizeof(int));

	ret = vfio_ub_init_cfg0_basic_info(vdev);
	if (ret)
		return ret;

	ret = vfio_ub_init_cfg0_cap_info(vdev);
	if (ret)
		goto out_uninit_cfg0_basic;

	ret = vfio_ub_init_cfg1_basic_info(vdev);
	if (ret)
		goto out_uninit_cfg0_cap;

	ret = vfio_ub_init_cfg1_cap_info(vdev);
	if (ret)
		goto out_uninit_cfg1_basic;

	ret = vfio_ub_init_port_basic_info(vdev);
	if (ret)
		goto out_uninit_cfg1_cap;

	return 0;

out_uninit_cfg1_cap:
	vfio_ub_uninit_cfg1_cap_info(vdev);
out_uninit_cfg1_basic:
	vfio_ub_uninit_cfg1_basic_info(vdev);
out_uninit_cfg0_cap:
	vfio_ub_uninit_cfg0_cap_info(vdev);
out_uninit_cfg0_basic:
	vfio_ub_uninit_cfg0_basic_info(vdev);

	memset(vdev->vconfig.map, CFG_MAP_INVALID, (UB_CFG1_MAX_CAP + 1) * sizeof(int));
	kfree(vdev->vconfig.slice);
	vdev->vconfig.slice = NULL;
	vdev->vconfig.nums = 0;
	return ret;
}

void vfio_ub_config_uninit(struct vfio_ub_core_device *vdev)
{
	if (vdev->vconfig.slice == NULL)
		return;

	vfio_ub_uninit_port_basic_info(vdev);
	vfio_ub_uninit_cfg1_cap_info(vdev);
	vfio_ub_uninit_cfg1_basic_info(vdev);
	vfio_ub_uninit_cfg0_cap_info(vdev);
	vfio_ub_uninit_cfg0_basic_info(vdev);

	memset(vdev->vconfig.map, CFG_MAP_INVALID, (UB_CFG1_MAX_CAP + 1) * sizeof(int));
	kfree(vdev->vconfig.slice);
	vdev->vconfig.slice = NULL;
	vdev->vconfig.nums = 0;
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

// SPDX-License-Identifier: GPL-2.0+
/* Copyright(c) 2025 HiSilicon Technologies CO., Ltd. All rights reserved. */

#define dev_fmt(fmt) "UDMA: " fmt

#include <linux/slab.h>
#include <linux/ummu_core.h>
#include "udma_common.h"
#include "udma_cmd.h"
#include "udma_eid.h"
#include "udma_tid.h"
#include "udma_segment.h"

static int udma_pin_segment(struct ubcore_device *ub_dev, struct ubcore_seg_cfg *cfg,
			    struct udma_segment *seg)
{
	struct udma_dev *udma_dev = to_udma_dev(ub_dev);
	struct udma_umem_param param;
	int ret = 0;

	if (cfg->flag.bs.non_pin)
		return 0;

	param.ub_dev = ub_dev;
	param.va = cfg->va;
	param.len = cfg->len;

	param.flag.bs.writable =
		!!(cfg->flag.bs.access & UBCORE_ACCESS_WRITE);
	param.flag.bs.non_pin = cfg->flag.bs.non_pin;
	param.is_kernel = seg->kernel_mode;

	seg->umem = udma_umem_get(&param);
	if (IS_ERR(seg->umem)) {
		ret = PTR_ERR(seg->umem);
		dev_err(udma_dev->dev,
			"failed to get segment umem, ret = %d.\n", ret);
	}

	return ret;
}

static void udma_unpin_seg(struct udma_segment *seg)
{
	if (!seg->core_tseg.seg.attr.bs.non_pin)
		udma_umem_release(seg->umem, seg->kernel_mode);
}

static int udma_check_seg_cfg(struct udma_dev *udma_dev, struct ubcore_seg_cfg *cfg)
{
	if (!cfg->token_id || cfg->flag.bs.access >= UDMA_SEGMENT_ACCESS_GUARD ||
	    cfg->flag.bs.token_policy >= UBCORE_TOKEN_SIGNED) {
		dev_err(udma_dev->dev,
			"error segment input, access = %d, token_policy = %d, or null key_id.\n",
			cfg->flag.bs.access, cfg->flag.bs.token_policy);
		return -EINVAL;
	}

	return 0;
}

static void udma_init_seg_cfg(struct udma_segment *seg, struct ubcore_seg_cfg *cfg)
{
	seg->core_tseg.token_id = cfg->token_id;
	seg->core_tseg.seg.token_id = cfg->token_id->token_id;
	seg->core_tseg.seg.attr.value = cfg->flag.value;
	seg->token_value = cfg->token_value.token;
	seg->token_value_valid = cfg->flag.bs.token_policy != UBCORE_TOKEN_NONE;
}

static int udma_u_get_seg_perm(struct ubcore_seg_cfg *cfg)
{
	if (cfg->flag.bs.access & UBCORE_ACCESS_LOCAL_ONLY ||
	    cfg->flag.bs.access & UBCORE_ACCESS_ATOMIC)
		return UMMU_DEV_ATOMIC | UMMU_DEV_WRITE | UMMU_DEV_READ;

	if (cfg->flag.bs.access & UBCORE_ACCESS_WRITE)
		return UMMU_DEV_WRITE | UMMU_DEV_READ;

	return UMMU_DEV_READ;
}

static int udma_sva_grant(struct ubcore_seg_cfg *cfg, struct iommu_sva *ksva)
{
#define UDMA_TOKEN_VALUE_INPUT 0
	struct ummu_seg_attr seg_attr = {.token = NULL, .e_bit = UMMU_EBIT_ON};
	struct ummu_token_info token_info;
	int perm;
	int ret;

	perm = udma_u_get_seg_perm(cfg);
	seg_attr.e_bit = (enum ummu_ebit_state)cfg->flag.bs.access &
			  UBCORE_ACCESS_LOCAL_ONLY;

	if (cfg->flag.bs.token_policy == UBCORE_TOKEN_NONE) {
		return ummu_sva_grant_range(ksva, (void *)(uintptr_t)cfg->va, cfg->len,
					    perm, (void *)&seg_attr);
	} else {
		seg_attr.token = &token_info;
		token_info.input = UDMA_TOKEN_VALUE_INPUT;
		token_info.tokenVal = cfg->token_value.token;
		ret = ummu_sva_grant_range(ksva, (void *)(uintptr_t)cfg->va, cfg->len,
					    perm, (void *)&seg_attr);
		token_info.tokenVal = 0;

		return ret;
	}
}

static int udma_set_seg_eid(struct udma_dev *udma_dev, uint32_t eid_index,
			    union ubcore_eid *eid)
{
	struct ubase_mbx_attr mbox_attr = {};
	struct ubase_cmd_mailbox *mailbox;
	struct udma_seid_upi *seid_upi;
	int ret;

	if (udma_dev->is_ue) {
		dev_info(udma_dev->dev,
			"The ue does not support the delivery of seid(%u) mailbox.\n",
			eid_index);
		return 0;
	}

	mailbox = udma_alloc_cmd_mailbox(udma_dev);
	if (!mailbox) {
		dev_err(udma_dev->dev,
			"failed to alloc mailbox for get tp seid.\n");
		return -ENOMEM;
	}

	mbox_attr.tag = eid_index;
	mbox_attr.op = UDMA_CMD_READ_SEID_UPI;
	ret = udma_post_mbox(udma_dev, mailbox, &mbox_attr);
	if (ret) {
		dev_err(udma_dev->dev,
			"send tp eid table mailbox cmd failed, ret is %d.\n", ret);
	} else {
		seid_upi = (struct udma_seid_upi *)mailbox->buf;
		*eid = seid_upi->seid;
	}

	udma_free_cmd_mailbox(udma_dev, mailbox);

	return ret;
}

struct ubcore_target_seg *udma_register_seg(struct ubcore_device *ub_dev,
					    struct ubcore_seg_cfg *cfg,
					    struct ubcore_udata *udata)
{
	struct udma_dev *udma_dev = to_udma_dev(ub_dev);
	struct udma_tid *udma_tid;
	struct udma_segment *seg;
	struct iommu_sva *ksva;
	int ret = 0;

	ret = udma_check_seg_cfg(udma_dev, cfg);
	if (ret)
		return NULL;

	seg = kzalloc(sizeof(*seg), GFP_KERNEL);
	if (!seg)
		return NULL;

	seg->kernel_mode = udata == NULL;
	udma_init_seg_cfg(seg, cfg);

	ret = udma_pin_segment(ub_dev, cfg, seg);
	if (ret) {
		dev_err(udma_dev->dev, "pin segment failed, ret = %d.\n", ret);
		goto err_pin_seg;
	}

	if (udata)
		return &seg->core_tseg;

	udma_tid = to_udma_tid(seg->core_tseg.token_id);

	mutex_lock(&udma_dev->ksva_mutex);
	ksva = (struct iommu_sva *)xa_load(&udma_dev->ksva_table, udma_tid->tid);
	if (!ksva) {
		dev_err(udma_dev->dev,
			"unable to get ksva while register segment, token maybe is free.\n");
		goto err_load_ksva;
	}

	ret = udma_sva_grant(cfg, ksva);
	if (ret) {
		dev_err(udma_dev->dev,
			"ksva grant failed with token policy %d, ret = %d.\n",
			cfg->flag.bs.token_policy, ret);
		goto err_load_ksva;
	}
	mutex_unlock(&udma_dev->ksva_mutex);

	return &seg->core_tseg;

err_load_ksva:
	mutex_unlock(&udma_dev->ksva_mutex);
	udma_unpin_seg(seg);
err_pin_seg:
	kfree(seg);
	return NULL;
}

int udma_unregister_seg(struct ubcore_target_seg *ubcore_seg)
{
	struct udma_tid *udma_tid = to_udma_tid(ubcore_seg->token_id);
	struct udma_dev *udma_dev = to_udma_dev(ubcore_seg->ub_dev);
	struct udma_segment *seg = to_udma_seg(ubcore_seg);
	struct ummu_token_info token_info;
	struct iommu_sva *ksva;
	int ret;

	if (!seg->kernel_mode)
		goto common_process;

	mutex_lock(&udma_dev->ksva_mutex);
	ksva = (struct iommu_sva *)xa_load(&udma_dev->ksva_table, udma_tid->tid);

	if (!ksva) {
		dev_warn(udma_dev->dev,
			 "unable to get ksva while unregister segment, token maybe is free.\n");
	} else {
		if (seg->token_value_valid) {
			token_info.tokenVal = seg->token_value;
			ret = ummu_sva_ungrant_range(ksva,
						     (void *)(uintptr_t)ubcore_seg->seg.ubva.va,
						     ubcore_seg->seg.len, &token_info);
			token_info.tokenVal = 0;
		} else {
			ret = ummu_sva_ungrant_range(ksva,
						     (void *)(uintptr_t)ubcore_seg->seg.ubva.va,
						     ubcore_seg->seg.len, NULL);
		}
		if (ret) {
			mutex_unlock(&udma_dev->ksva_mutex);
			dev_err(udma_dev->dev, "unregister segment failed, ret = %d.\n", ret);
			return ret;
		}
	}
	mutex_unlock(&udma_dev->ksva_mutex);

common_process:
	udma_unpin_seg(seg);
	seg->token_value = 0;
	kfree(seg);

	return 0;
}

struct ubcore_target_seg *udma_import_seg(struct ubcore_device *dev,
					  struct ubcore_target_seg_cfg *cfg,
					  struct ubcore_udata *udata)
{
	struct udma_dev *udma_dev = to_udma_dev(dev);
	struct udma_segment *seg;

	if (cfg->seg.attr.bs.token_policy > UBCORE_TOKEN_PLAIN_TEXT) {
		dev_err(udma_dev->dev, "invalid token policy = %d.\n",
			cfg->seg.attr.bs.token_policy);
		return NULL;
	}

	seg = kzalloc(sizeof(*seg), GFP_KERNEL);
	if (!seg)
		return NULL;

	if (cfg->seg.attr.bs.token_policy != UBCORE_TOKEN_NONE) {
		seg->token_value = cfg->token_value.token;
		seg->token_value_valid = true;
	}

	seg->tid = cfg->seg.token_id >> UDMA_TID_SHIFT;

	return &seg->core_tseg;
}

int udma_unimport_seg(struct ubcore_target_seg *tseg)
{
	struct udma_segment *seg;

	seg = to_udma_seg(tseg);
	seg->token_value = 0;
	kfree(seg);

	return 0;
}

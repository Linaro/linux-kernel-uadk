// SPDX-License-Identifier: GPL-2.0+
/* Copyright(c) 2025 HiSilicon Technologies CO., Ltd. All rights reserved. */

#define dev_fmt(fmt) "UDMA: " fmt

#include <asm/current.h>
#include <ub/urma/ubcore_api.h>
#include "udma_jfs.h"
#include "udma_jetty.h"
#include "udma_ctrlq_tp.h"
#include "udma_ctx.h"

static int udma_init_ctx_resp(struct udma_dev *dev, struct ubcore_udrv_priv *udrv_data)
{
	struct udma_create_ctx_resp resp;
	unsigned long byte;

	if (!udrv_data->out_addr ||
	    udrv_data->out_len < sizeof(resp)) {
		dev_err(dev->dev,
			"Invalid ctx resp out: len %d or addr is invalid.\n",
			udrv_data->out_len);
		return -EINVAL;
	}

	resp.cqe_size = dev->caps.cqe_size;
	resp.dwqe_enable = !!(dev->caps.feature & UDMA_CAP_FEATURE_DIRECT_WQE);
	resp.reduce_enable = !!(dev->caps.feature & UDMA_CAP_FEATURE_REDUCE);
	resp.ue_id = dev->ue_id;
	resp.chip_id = dev->chip_id;
	resp.die_id = dev->die_id;
	resp.dump_aux_info = dump_aux_info;
	resp.jfr_sge = dev->caps.jfr_sge;

	byte = copy_to_user((void *)(uintptr_t)udrv_data->out_addr, &resp,
			   (uint32_t)sizeof(resp));
	if (byte) {
		dev_err(dev->dev,
			"copy ctx resp to user failed, byte = %lu.\n", byte);
		return -EFAULT;
	}

	return 0;
}

struct ubcore_ucontext *udma_alloc_ucontext(struct ubcore_device *ub_dev,
					    uint32_t eid_index,
					    struct ubcore_udrv_priv *udrv_data)
{
	struct udma_dev *dev = to_udma_dev(ub_dev);
	struct udma_context *ctx;
	int ret;

	ctx = kzalloc(sizeof(struct udma_context), GFP_KERNEL);
	if (ctx == NULL)
		return NULL;

	ctx->sva = ummu_sva_bind_device(dev->dev, current->mm, NULL);
	if (!ctx->sva) {
		dev_err(dev->dev, "SVA failed to bind device.\n");
		goto err_free_ctx;
	}

	ret = ummu_get_tid(dev->dev, ctx->sva, &ctx->tid);
	if (ret) {
		dev_err(dev->dev, "Failed to get tid.\n");
		goto err_unbind_dev;
	}

	ctx->dev = dev;
	INIT_LIST_HEAD(&ctx->pgdir_list);
	mutex_init(&ctx->pgdir_mutex);

	ret = udma_init_ctx_resp(dev, udrv_data);
	if (ret) {
		dev_err(dev->dev, "Init ctx resp failed.\n");
		goto err_init_ctx_resp;
	}

	return &ctx->base;

err_init_ctx_resp:
	mutex_destroy(&ctx->pgdir_mutex);
err_unbind_dev:
	ummu_sva_unbind_device(ctx->sva);
err_free_ctx:
	kfree(ctx);
	return NULL;
}

int udma_free_ucontext(struct ubcore_ucontext *ucontext)
{
	struct udma_dev *udma_dev = to_udma_dev(ucontext->ub_dev);
	struct udma_context *ctx;
	int ret;

	ctx = to_udma_context(ucontext);

	ret = ummu_core_invalidate_cfg_table(ctx->tid);
	if (ret)
		dev_err(udma_dev->dev, "invalidate cfg_table failed, ret=%d.\n", ret);

	mutex_destroy(&ctx->pgdir_mutex);
	ummu_sva_unbind_device(ctx->sva);

	kfree(ctx);

	return 0;
}

int udma_mmap(struct ubcore_ucontext *uctx, struct vm_area_struct *vma)
{
#define JFC_DB_UNMAP_BOUND 1
	struct udma_dev *udma_dev = to_udma_dev(uctx->ub_dev);
	struct ubcore_ucontext *jetty_uctx;
	struct udma_jetty_queue *sq;
	resource_size_t db_addr;
	uint64_t address;
	uint64_t j_id;
	uint32_t cmd;

	if (((vma->vm_end - vma->vm_start) % PAGE_SIZE) != 0) {
		dev_err(udma_dev->dev,
			"mmap failed, unexpected vm area size.\n");
		return -EINVAL;
	}

	db_addr = udma_dev->db_base;
	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);

	cmd = get_mmap_cmd(vma);
	switch (cmd) {
	case UDMA_MMAP_JFC_PAGE:
		if (io_remap_pfn_range(vma, vma->vm_start,
				       jfc_arm_mode > JFC_DB_UNMAP_BOUND ?
				       (uint64_t)db_addr >> PAGE_SHIFT :
				       page_to_pfn(udma_dev->db_page),
				       PAGE_SIZE, vma->vm_page_prot))
			return -EAGAIN;
		break;
	case UDMA_MMAP_JETTY_DSQE:
		j_id = get_mmap_idx(vma);
		xa_lock(&udma_dev->jetty_table.xa);
		sq = xa_load(&udma_dev->jetty_table.xa, j_id);
		if (!sq) {
			dev_err(udma_dev->dev,
				"mmap failed, j_id: %llu not exist\n", j_id);
			xa_unlock(&udma_dev->jetty_table.xa);
			return -EINVAL;
		}

		if (sq->is_jetty)
			jetty_uctx = to_udma_jetty_from_queue(sq)->ubcore_jetty.uctx;
		else
			jetty_uctx = to_udma_jfs_from_queue(sq)->ubcore_jfs.uctx;

		if (jetty_uctx != uctx) {
			dev_err(udma_dev->dev,
				"mmap failed, j_id: %llu, uctx invalid\n", j_id);
			xa_unlock(&udma_dev->jetty_table.xa);
			return -EINVAL;
		}
		xa_unlock(&udma_dev->jetty_table.xa);

		address = (uint64_t)db_addr + JETTY_DSQE_OFFSET + j_id * UDMA_HW_PAGE_SIZE;

		if (io_remap_pfn_range(vma, vma->vm_start, address >> PAGE_SHIFT,
				       PAGE_SIZE, vma->vm_page_prot))
			return -EAGAIN;
		break;
	default:
		dev_err(udma_dev->dev,
			"mmap failed, cmd(%u) not support\n", cmd);
		return -EINVAL;
	}

	return 0;
}

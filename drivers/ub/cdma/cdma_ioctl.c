// SPDX-License-Identifier: GPL-2.0+
/* Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved. */

#define dev_fmt(fmt) "CDMA: " fmt

#include <ub/ubus/ubus.h>

#include <uapi/ub/cdma/cdma_abi.h>
#include "cdma.h"
#include "cdma_context.h"
#include "cdma_types.h"
#include "cdma_queue.h"
#include "cdma_jfc.h"
#include "cdma_uobj.h"
#include "cdma_ioctl.h"

typedef int (*cdma_cmd_handler)(struct cdma_ioctl_hdr *hdr,
				struct cdma_file *cfile);

static void cdma_fill_device_attr(struct cdma_dev *cdev,
				  struct cdma_device_cap *dev_cap)
{
	dev_cap->max_jfc = cdev->caps.jfc.max_cnt;
	dev_cap->max_jfs = cdev->caps.jfs.max_cnt;
	dev_cap->max_jfc_depth = cdev->caps.jfc.depth;
	dev_cap->max_jfs_depth = cdev->caps.jfs.depth;
	dev_cap->max_jfs_sge = cdev->caps.jfs_sge;
	dev_cap->max_jfs_rsge = cdev->caps.jfs_rsge;
	dev_cap->max_msg_size = cdev->caps.max_msg_len;
	dev_cap->trans_mode = cdev->caps.trans_mode;
	dev_cap->ceq_cnt = cdev->caps.comp_vector_cnt;
}

static int cdma_query_dev(struct cdma_ioctl_hdr *hdr, struct cdma_file *cfile)
{
	struct cdma_cmd_query_device_attr_args args = { 0 };
	struct cdma_dev *cdev = cfile->cdev;
	unsigned long ret;

	if (!hdr->args_addr || hdr->args_len < sizeof(args))
		return -EINVAL;

	args.out.attr.eid.dw0 = cdev->eid;
	args.out.attr.eu_num = cdev->base.attr.eu_num;
	memcpy(args.out.attr.eus, cdev->base.attr.eus,
	       sizeof(struct eu_info) * cdev->base.attr.eu_num);
	cdma_fill_device_attr(cdev, &args.out.attr.dev_cap);

	ret = copy_to_user((void __user *)(uintptr_t)hdr->args_addr, &args,
				(u32)sizeof(args));
	if (ret) {
		dev_err(cdev->dev, "query dev copy to user failed, ret = %lu\n",
			ret);
		return -EFAULT;
	}

	return 0;
}

static int cdma_create_ucontext(struct cdma_ioctl_hdr *hdr,
				struct cdma_file *cfile)
{
	struct cdma_create_context_args args = { 0 };
	struct cdma_dev *cdev = cfile->cdev;
	struct cdma_context *ctx;
	int ret;

	if (cfile->uctx) {
		dev_err(cdev->dev, "create jfae failed, ctx handle = %d.\n",
			ctx->handle);
		return -EEXIST;
	}

	if (!hdr->args_addr || hdr->args_len < sizeof(args))
		return -EINVAL;

	ret = (int)copy_from_user(&args, (void *)hdr->args_addr,
				  (u32)sizeof(args));
	if (ret) {
		dev_err(cdev->dev, "get user data failed, ret = %d.\n", ret);
		return ret;
	}

	ctx = cdma_alloc_context(cdev, false);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	args.out.cqe_size = cdev->caps.cqe_size;
	args.out.dwqe_enable =
		!!(cdev->caps.feature & CDMA_CAP_FEATURE_DIRECT_WQE);
	cfile->uctx = ctx;

	ret = (int)copy_to_user((void *)hdr->args_addr, &args,
				(u32)sizeof(args));
	if (ret) {
		dev_err(cdev->dev, "copy ctx to user failed, ret = %d.\n", ret);
		goto free_context;
	}

	return ret;

free_context:
	cfile->uctx = NULL;
	cdma_free_context(cdev, ctx);

	return ret;
}

static int cdma_delete_ucontext(struct cdma_ioctl_hdr *hdr,
				struct cdma_file *cfile)
{
	struct cdma_dev *cdev = cfile->cdev;

	if (!cfile->uctx) {
		dev_err(cdev->dev, "cdma context has not been created.\n");
		return -ENOENT;
	}
	if (!list_empty(&cfile->uctx->queue_list)) {
		dev_err(cdev->dev,
			"queue/segment is still in use, ctx handle = %d.\n",
			cfile->uctx->handle);
		return -EBUSY;
	}

	cdma_free_context(cdev, cfile->uctx);
	cfile->uctx = NULL;

	return 0;
}

static int cdma_cmd_create_queue(struct cdma_ioctl_hdr *hdr, struct cdma_file *cfile)
{
	struct cdma_cmd_create_queue_args arg = { 0 };
	struct cdma_dev *cdev = cfile->cdev;
	struct queue_cfg cfg;
	struct cdma_queue *queue;
	struct cdma_uobj *uobj;
	int ret;

	if (!hdr->args_addr || hdr->args_len != sizeof(arg) || !cfile->uctx)
		return -EINVAL;

	ret = (int)copy_from_user(&arg, (void *)hdr->args_addr,
				  (u32)sizeof(arg));
	if (ret) {
		dev_err(cdev->dev, "create queue get user data failed, ret = %d.\n", ret);
		return -EFAULT;
	}

	cfg = (struct queue_cfg) {
		.queue_depth = arg.in.queue_depth,
		.dcna = arg.in.dcna,
		.priority = arg.in.priority,
		.rmt_eid.dw0 = arg.in.rmt_eid,
		.user_ctx = arg.in.user_ctx,
		.trans_mode = arg.in.trans_mode,
	};

	uobj = cdma_uobj_create(cfile, UOBJ_TYPE_QUEUE);
	if (IS_ERR(uobj)) {
		dev_err(cdev->dev, "create queue uobj failed.\n");
		return -ENOMEM;
	}

	queue = cdma_create_queue(cdev, cfile->uctx, &cfg, 0, false);
	if (!queue) {
		dev_err(cdev->dev, "create queue failed.\n");
		ret = -EINVAL;
		goto err_create_queue;
	}

	uobj->object = queue;
	arg.out.queue_id = queue->id;
	arg.out.handle = uobj->id;
	ret = (int)copy_to_user((void *)hdr->args_addr, &arg, (u32)sizeof(arg));
	if (ret) {
		dev_err(cdev->dev, "create queue copy to user failed, ret = %d.\n", ret);
		ret = -EFAULT;
		goto err_copy_to_user;
	}
	list_add_tail(&queue->list, &cfile->uctx->queue_list);

	return 0;
err_copy_to_user:
	cdma_delete_queue(cdev, queue->id);
err_create_queue:
	cdma_uobj_delete(uobj);
	return ret;
}

static int cdma_cmd_delete_queue(struct cdma_ioctl_hdr *hdr, struct cdma_file *cfile)
{
	struct cdma_cmd_delete_queue_args arg = { 0 };
	struct cdma_dev *cdev = cfile->cdev;
	struct cdma_queue *queue;
	struct cdma_uobj *uobj;
	int ret;

	if (!hdr->args_addr || hdr->args_len != sizeof(arg))
		return -EINVAL;

	ret = (int)copy_from_user(&arg, (void *)hdr->args_addr,
				  (u32)sizeof(arg));
	if (ret) {
		dev_err(cdev->dev, "delete queue get user data failed, ret = %d.\n", ret);
		return -EFAULT;
	}

	uobj = cdma_uobj_get(cfile, arg.in.handle, UOBJ_TYPE_QUEUE);
	if (IS_ERR(uobj)) {
		dev_err(cdev->dev, "get queue uobj failed, handle = %llu.\n",
			arg.in.handle);
		return -EINVAL;
	}

	queue = (struct cdma_queue *)uobj->object;
	if (queue->jfc) {
		dev_err(cdev->dev, "jfc is still in use.");
		return -EBUSY;
	}

	cdma_uobj_delete(uobj);
	list_del(&queue->list);
	ret = cdma_delete_queue(cdev, queue->id);
	if (ret)
		dev_err(cdev->dev, "delete queue failed, ret = %d.\n", ret);

	return ret;
}

static int cdma_cmd_delete_jfc(struct cdma_ioctl_hdr *hdr,
			       struct cdma_file *cfile)
{
	struct cdma_cmd_delete_jfc_args arg = { 0 };
	struct cdma_dev *cdev = cfile->cdev;
	struct cdma_base_jfc *base_jfc;
	struct cdma_queue *queue;
	struct cdma_uobj *uobj;
	int ret;

	if (!hdr->args_addr || hdr->args_len != (u32)sizeof(arg))
		return -EINVAL;

	ret = (int)copy_from_user(&arg, (void *)hdr->args_addr,
				  (u32)sizeof(arg));
	if (ret) {
		dev_err(cdev->dev, "get user data failed, ret = %d.\n", ret);
		return -EFAULT;
	}

	uobj = cdma_uobj_get(cfile, arg.in.queue_id, UOBJ_TYPE_QUEUE);
	if (IS_ERR(uobj)) {
		dev_err(cdev->dev,
			"delete jfc, get queue uobj failed, queue id = %u.\n",
			arg.in.queue_id);
		return -EINVAL;
	}
	queue = (struct cdma_queue *)uobj->object;

	uobj = cdma_uobj_get(cfile, arg.in.handle, UOBJ_TYPE_JFC);
	if (IS_ERR(uobj)) {
		dev_err(cdev->dev, "get jfc uobj failed.\n");
		return -EINVAL;
	}

	base_jfc = (struct cdma_base_jfc *)uobj->object;
	ret = cdma_delete_jfc(cdev, base_jfc->id, &arg);
	if (ret) {
		dev_err(cdev->dev, "cdma delete jfc failed, ret = %d.\n", ret);
		return -EFAULT;
	}

	cdma_set_queue_res(cdev, queue, QUEUE_RES_JFC, NULL);
	cdma_uobj_delete(uobj);

	ret = (int)copy_to_user((void *)hdr->args_addr, &arg, (u32)sizeof(arg));
	if (ret) {
		dev_err(cdev->dev,
			"delete jfc copy to user data failed, ret = %d.\n",
			ret);
		return -EFAULT;
	}

	return 0;
}

static cdma_cmd_handler g_cdma_cmd_handler[CDMA_CMD_MAX] = {
	[CDMA_CMD_QUERY_DEV_INFO] = cdma_query_dev,
	[CDMA_CMD_CREATE_CTX] = cdma_create_ucontext,
	[CDMA_CMD_DELETE_CTX] = cdma_delete_ucontext,
	[CDMA_CMD_CREATE_QUEUE] = cdma_cmd_create_queue,
	[CDMA_CMD_DELETE_QUEUE] = cdma_cmd_delete_queue,
	[CDMA_CMD_DELETE_JFC] = cdma_cmd_delete_jfc,
};

int cdma_cmd_parse(struct cdma_file *cfile, struct cdma_ioctl_hdr *hdr)
{
	struct cdma_dev *cdev = cfile->cdev;
	int ret;

	if (hdr->command >= CDMA_CMD_MAX || !g_cdma_cmd_handler[hdr->command]) {
		dev_err(cdev->dev,
			"invalid cdma user command or no handler, command = %u\n",
			hdr->command);
		return -EINVAL;
	}

	mutex_lock(&cfile->ctx_mutex);
	ret = g_cdma_cmd_handler[hdr->command](hdr, cfile);
	mutex_unlock(&cfile->ctx_mutex);

	return ret;
}

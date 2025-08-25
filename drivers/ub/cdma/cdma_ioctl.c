// SPDX-License-Identifier: GPL-2.0+
/* Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved. */

#define dev_fmt(fmt) "CDMA: " fmt

#include <ub/ubus/ubus.h>

#include <uapi/ub/cdma/cdma_abi.h>
#include "cdma.h"
#include "cdma_types.h"
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

static cdma_cmd_handler g_cdma_cmd_handler[CDMA_CMD_MAX] = {
	[CDMA_CMD_QUERY_DEV_INFO] = cdma_query_dev,
};

int cdma_cmd_parse(struct cdma_file *cfile, struct cdma_ioctl_hdr *hdr)
{
	struct cdma_dev *cdev = cfile->cdev;

	if (hdr->command >= CDMA_CMD_MAX || !g_cdma_cmd_handler[hdr->command]) {
		dev_err(cdev->dev,
			"invalid cdma user command or no handler, command = %u\n",
			hdr->command);
		return -EINVAL;
	}

	return g_cdma_cmd_handler[hdr->command](hdr, cfile);
}

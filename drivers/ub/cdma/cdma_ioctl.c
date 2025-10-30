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

static cdma_cmd_handler g_cdma_cmd_handler[CDMA_CMD_MAX] = {
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

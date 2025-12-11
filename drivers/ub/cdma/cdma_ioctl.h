/* SPDX-License-Identifier: GPL-2.0+ */
/* Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved. */

#ifndef __CDMA_IOCTL_H__
#define __CDMA_IOCTL_H__

struct cdma_file;
struct cdma_ioctl_hdr;

int cdma_cmd_parse(struct cdma_file *cfile, struct cdma_ioctl_hdr *hdr);

#endif /* _CDMA_IOCTL_H_ */

/* SPDX-License-Identifier: GPL-2.0+ */
/* Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved. */

#ifndef __CDMA_CHARDEV_H__
#define __CDMA_CHARDEV_H__

#define CDMA_TEST_NAME "cdma_dev"
#define CDMA_MAX_DEVICES 1
#define CDMA_JETTY_DSQE_OFFSET 0x1000

extern struct class *cdma_cdev_class;

struct cdma_dev;

void cdma_destroy_chardev(struct cdma_dev *cdev);
int cdma_create_chardev(struct cdma_dev *cdev);
void cdma_release_file(struct kref *ref);

#endif /* _CDMA_CHARDEV_H_ */

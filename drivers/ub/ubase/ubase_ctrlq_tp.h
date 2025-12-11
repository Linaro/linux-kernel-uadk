/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved.
 *
 */

#ifndef __UBASE_CTRLQ_TP_H__
#define __UBASE_CTRLQ_TP_H__

#include "ubase_dev.h"

int ubase_notify_tp_fd_by_ctrlq(struct ubase_dev *udev, u32 tp_num);
void ubase_dev_uninit_rack_tp_tpg(struct ubase_dev *udev);
int ubase_dev_init_rack_tp_tpg(struct ubase_dev *udev);

#endif

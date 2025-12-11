/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved.
 *
 */

#ifndef __UBASE_ARQ_H__
#define __UBASE_ARQ_H__

#include <linux/types.h>

#include "ubase_dev.h"

void ubase_arq_init(struct ubase_dev *udev);
void ubase_arq_uninit(struct ubase_dev *udev);
void ubase_add_to_arq(struct ubase_dev *udev, u16 opcode, void *msg_data,
		      u32 msg_data_len);
bool ubase_is_arq_msg(u16 opcode);
void ubase_cmd_arq_handler(struct ubase_delay_work *ubase_work);

#endif

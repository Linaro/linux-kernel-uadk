/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved.
 *
 */

#ifndef __UBASE_CMD_H__
#define __UBASE_CMD_H__

#include <ub/ubase/ubase_comm_cmd.h>

#include "ubase_dev.h"

int __ubase_register_crq_event(struct ubase_dev *udev,
			       struct ubase_crq_event_nb *nb);
void __ubase_unregister_crq_event(struct ubase_dev *udev, u16 opcode);

#endif

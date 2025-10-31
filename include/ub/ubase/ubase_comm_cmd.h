/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved.
 *
 */

#ifndef _UB_UBASE_COMM_CMD_H_
#define _UB_UBASE_COMM_CMD_H_

#include <linux/auxiliary_bus.h>
#include <linux/types.h>

struct ubase_crq_event_nb {
	u16 opcode;
	void *back;
	int (*crq_handler)(void *dev, void *data, u32 len);
};

int ubase_register_crq_event(struct auxiliary_device *aux_dev,
			     struct ubase_crq_event_nb *nb);
void ubase_unregister_crq_event(struct auxiliary_device *aux_dev, u16 opcode);

#endif

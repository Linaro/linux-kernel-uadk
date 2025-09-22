/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) HiSilicon Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef __LINK_H__
#define __LINK_H__

enum ub_link_event {
	UB_LINK_UP,
	UB_LINK_DOWN
};

void ub_link_change_handler(struct work_struct *work);
void ublc_link_up_handle(struct ub_port *port);
void ublc_link_down_handle(struct ub_port *port);

#endif /* __LINK_H__ */

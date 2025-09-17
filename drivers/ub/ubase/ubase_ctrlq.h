/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved.
 *
 */

#ifndef __UBASE_CTRLQ_H__
#define __UBASE_CTRLQ_H__

#include <ub/ubase/ubase_comm_ctrlq.h>

#include "ubase_dev.h"

#define UBASE_CTRLQ_TX_TIMEOUT		30000
#define UBASE_CTRLQ_BB_LEN		32U
#define UBASE_CTRLQ_HDR_LEN		12
#define UBASE_CTRLQ_DATA_LEN		(UBASE_CTRLQ_BB_LEN - UBASE_CTRLQ_HDR_LEN)
#define UBASE_CTRLQ_MAX_BB		32
#define UBASE_CTRLQ_SCHED_TIMEOUT	(HZ / 2)
#define UBASE_CTRLQ_SEQ_MASK		BIT(15)
#define UBASE_CTRLQ_DEAD_TIME		40000

enum ubase_ctrlq_state {
	UBASE_CTRLQ_STATE_ENABLE,
};

struct ubase_ctrlq_ue_info {
	u8	mbx_ue_id;
	u16	seq;
	u16	bus_ue_id;
};

int ubase_ctrlq_init(struct ubase_dev *udev);
void ubase_ctrlq_uninit(struct ubase_dev *udev);
void ubase_ctrlq_disable(struct ubase_dev *udev);

int __ubase_ctrlq_send(struct ubase_dev *udev, struct ubase_ctrlq_msg *msg,
		       struct ubase_ctrlq_ue_info *ue_info);

void ubase_ctrlq_service_task(struct ubase_delay_work *ubase_work);
void ubase_ctrlq_handle_crq_msg(struct ubase_dev *udev,
				struct ubase_ctrlq_base_block *head,
				u16 seq, void *msg, u16 data_len);
void ubase_ctrlq_clean_service_task(struct ubase_delay_work *ubase_work);

#endif

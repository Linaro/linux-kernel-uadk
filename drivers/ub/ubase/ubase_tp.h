/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved.
 *
 */

#ifndef __UBASE_TP_H__
#define __UBASE_TP_H__

#include <linux/netdevice.h>

#include "ubase_dev.h"

#define UBASE_WAIT_TP_FLUSH_TOTAL_STEPS	12

struct ubase_tpg {
	u32		mb_tpgn;
	u8		tpg_state;
	u8		vl;
	atomic_t	tp_fd_cnt;
	u8		tp_cnt;
	unsigned long	valid_tp;
	u32		start_tpn;
	u32		tp_shift;
};

int ubase_ae_tp_flush_done(struct notifier_block *nb, unsigned long event,
			   void *data);
int ubase_dev_init_tp_tpg(struct ubase_dev *udev);
void ubase_dev_uninit_tp_tpg(struct ubase_dev *udev);

#endif

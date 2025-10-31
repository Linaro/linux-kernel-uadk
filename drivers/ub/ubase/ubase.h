/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved.
 *
 */

#ifndef __UBASE_H__
#define __UBASE_H__

#include <linux/bitmap.h>
#include <linux/io.h>

#define UBASE_CAP_LEN			3
#define UBASE_MAX_TCG_NUM		(4)

struct ubase_delay_work {
	struct delayed_work	service_task;
	unsigned long		state;
};

enum {
	UBASE_SUPPORT_UBL_B		= 0,
	UBASE_SUPPORT_TA_EXTDB_BUF_B	= 2,
	UBASE_SUPPORT_TA_TIMER_BUF_B	= 3,
	UBASE_SUPPORT_ERR_HANDLE_B	= 4,
	UBASE_SUPPORT_CTRLQ_B		= 5,
	UBASE_SUPPORT_ETH_MAC_B		= 6,
	UBASE_SUPPORT_MAC_STATS_B	= 10,
	UBASE_SUPPORT_PRE_ALLOC_B		= 13,
	UBASE_SUPPORT_UDMA_DISABLE_B		= 14,
	UBASE_SUPPORT_UNIC_DISABLE_B		= 15,
	UBASE_SUPPORT_UVB_B			= 16,
	UBASE_SUPPORT_IP_OVER_URMA_B		= 17,
	UBASE_SUPPORT_IP_OVER_URMA_UTP_B	= 18,
	UBASE_SUPPORT_ACTIVATE_PROXY_B		= 19,
	UBASE_SUPPORT_UTP_B			= 20,

	/* must be last entry and it should <= UBASE_CAP_LEN * 32 */
	UBASE_SUPPORT_MASK_NBITS
};

#define ubase_get_cap_bit(udev, nr) \
	test_bit(nr, (unsigned long *)((udev)->cap_bits))

#endif

/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved.
 *
 */

#ifndef __UNIC_STATS_H__
#define __UNIC_STATS_H__

#include <linux/ethtool.h>
#include <linux/netdevice.h>
#include <ub/ubase/ubase_comm_cmd.h>
#include <ub/ubase/ubase_comm_ctrlq.h>

#define UNIC_FEC_CORR_BLOCKS	BIT(0)
#define UNIC_FEC_UNCORR_BLOCKS	BIT(1)
#define UNIC_FEC_CORR_BITS	BIT(2)

void unic_get_fec_stats(struct net_device *ndev,
			struct ethtool_fec_stats *fec_stats);

#endif /* __UNIC_STATS_H__ */

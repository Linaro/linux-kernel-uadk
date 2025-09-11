// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved.
 *
 */

#include <linux/phy.h>
#include <ub/ubase/ubase_comm_cmd.h>
#include <ub/ubase/ubase_comm_stats.h>

#include "unic.h"
#include "unic_dev.h"
#include "unic_hw.h"
#include "unic_netdev.h"
#include "unic_stats.h"

static void unic_get_fec_stats_total(struct unic_dev *unic_dev, u8 stats_flags,
				     struct ethtool_fec_stats *fec_stats)
{
	struct unic_fec_stats_item *total = &unic_dev->stats.fec_stats.total;

	if (stats_flags & UNIC_FEC_CORR_BLOCKS)
		fec_stats->corrected_blocks.total = total->corr_blocks;
	if (stats_flags & UNIC_FEC_UNCORR_BLOCKS)
		fec_stats->uncorrectable_blocks.total = total->uncorr_blocks;
	if (stats_flags & UNIC_FEC_CORR_BITS)
		fec_stats->corrected_bits.total = total->corr_bits;
}

static void unic_get_ubl_fec_stats(struct unic_dev *unic_dev,
				   struct ethtool_fec_stats *fec_stats)
{
	u32 fec_mode = unic_dev->hw.mac.fec_mode;
	u8 stats_flags = 0;

	switch (fec_mode) {
	case ETHTOOL_FEC_RS:
		stats_flags = UNIC_FEC_UNCORR_BLOCKS | UNIC_FEC_CORR_BITS;
		unic_get_fec_stats_total(unic_dev, stats_flags, fec_stats);
		break;
	default:
		unic_err(unic_dev,
			 "fec stats is not supported in mode(0x%x).\n",
			 fec_mode);
		break;
	}
}

void unic_get_fec_stats(struct net_device *ndev,
			struct ethtool_fec_stats *fec_stats)
{
	struct unic_dev *unic_dev = netdev_priv(ndev);

	if (!unic_dev_fec_stats_supported(unic_dev) ||
	    unic_dev->hw.mac.fec_mode == ETHTOOL_FEC_OFF)
		return;

	if (unic_update_fec_stats(unic_dev))
		return;

	if (unic_dev_ubl_supported(unic_dev))
		unic_get_ubl_fec_stats(unic_dev, fec_stats);
}

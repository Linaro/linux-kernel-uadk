// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved.
 *
 */

#include "unic_channel.h"
#include "unic_dev.h"
#include "unic_netdev.h"

void unic_get_channels(struct net_device *ndev,
		       struct ethtool_channels *ch)
{
	struct unic_dev *unic_dev = netdev_priv(ndev);

	ch->max_combined = unic_channels_max_num(unic_dev->comdev.adev);
	ch->combined_count = unic_dev->channels.rss_size;
}

static int unic_check_rss_size_param(struct unic_dev *unic_dev, u32 new_rss_size,
				     u32 org_rss_size)
{
	u32 max_rss_size;

	if (org_rss_size == new_rss_size) {
		unic_err(unic_dev,
			 "old num and new num are the same, rss_size = %u.\n",
			 org_rss_size);
		return -EINVAL;
	}

	max_rss_size = unic_get_max_rss_size(unic_dev);
	if (new_rss_size < 1 || new_rss_size > max_rss_size) {
		unic_err(unic_dev,
			 "the rss_size(%u) is out of the range [1, %u].\n",
			 new_rss_size, max_rss_size);
		return -EINVAL;
	}

	if (max_rss_size % new_rss_size) {
		unic_err(unic_dev,
			 "the rss_size(%u) can't distributed to max_rss_size(%u).\n",
			 new_rss_size, max_rss_size);
		return -EINVAL;
	}

	return 0;
}

static int unic_check_set_channels_available(struct net_device *ndev)
{
	struct unic_dev *unic_dev = netdev_priv(ndev);

	if (netif_running(ndev)) {
		unic_err(unic_dev, "failed to set channels, due to network interface is up, please down it first and try again.\n");
		return -EBUSY;
	}

	if (unic_resetting(ndev)) {
		unic_err(unic_dev,
			 "failed to set channels, due to dev resetting.\n");
		return -EBUSY;
	}

	return 0;
}

int unic_set_channels(struct net_device *ndev,
		      struct ethtool_channels *ch)
{
	struct unic_dev *unic_dev = netdev_priv(ndev);
	u32 org_rss_size = unic_dev->channels.rss_size;
	u32 new_rss_size = ch->combined_count;
	int ret, ret1;

	ret = unic_check_set_channels_available(ndev);
	if (ret)
		return ret;

	ret = unic_check_rss_size_param(unic_dev, new_rss_size, org_rss_size);
	if (ret)
		return ret;

	ret = unic_change_rss_size(unic_dev, new_rss_size, org_rss_size);
	if (ret) {
		unic_err(unic_dev,
			 "failed to change rss_size, revert to old rss_size, ret = %d.\n",
			 ret);

		ret1 = unic_change_rss_size(unic_dev, org_rss_size, org_rss_size);
		if (ret1) {
			unic_err(unic_dev,
				 "failed to revert to old rss_size, ret1 = %d.\n",
				 ret1);
			return ret1;
		}
	}

	return ret;
}

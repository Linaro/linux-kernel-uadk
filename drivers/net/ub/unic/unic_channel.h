/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved.
 *
 */

#ifndef __UNIC_CHANNEL_H__
#define __UNIC_CHANNEL_H__

#include <linux/ethtool.h>
#include <linux/netdevice.h>

struct unic_channel_param {
	u32 sqebb_depth;
	u32 rqe_depth;
	u32 rx_buff_len;
};

int unic_set_channels(struct net_device *ndev, struct ethtool_channels *ch);
void unic_get_channels(struct net_device *ndev, struct ethtool_channels *ch);
int unic_set_channels_param(struct net_device *ndev,
			    struct ethtool_ringparam *param,
			    struct kernel_ethtool_ringparam *kernel_param,
			    struct netlink_ext_ack *extack);
void unic_get_channels_param(struct net_device *ndev,
			     struct ethtool_ringparam *param,
			     struct kernel_ethtool_ringparam *kernel_param,
			     struct netlink_ext_ack *extack);

#endif

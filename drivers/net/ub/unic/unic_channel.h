/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved.
 *
 */

#ifndef __UNIC_CHANNEL_H__
#define __UNIC_CHANNEL_H__

#include <linux/ethtool.h>
#include <linux/netdevice.h>

int unic_set_channels(struct net_device *ndev, struct ethtool_channels *ch);
void unic_get_channels(struct net_device *ndev, struct ethtool_channels *ch);

#endif

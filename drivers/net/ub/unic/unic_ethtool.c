// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved.
 *
 */

#include <linux/phy.h>
#include <ub/ubase/ubase_comm_cmd.h>

#include "unic.h"
#include "unic_dev.h"
#include "unic_hw.h"
#include "unic_netdev.h"
#include "unic_stats.h"
#include "unic_ethtool.h"

static u32 unic_get_link_status(struct net_device *netdev)
{
	struct unic_dev *unic_dev = netdev_priv(netdev);

	unic_link_status_update(unic_dev);

	return unic_dev->sw_link_status;
}

static void unic_get_driver_info(struct net_device *netdev,
				 struct ethtool_drvinfo *drvinfo)
{
	struct unic_dev *unic_dev = netdev_priv(netdev);
	struct ubase_caps *caps = ubase_get_dev_caps(unic_dev->comdev.adev);
	u32 fw_version = 0;

	if (!caps)
		unic_err(unic_dev,
			 "failed to get fw version, use default value 0.\n");
	else
		fw_version = caps->fw_version;

	strscpy(drvinfo->version, UNIC_MOD_VERSION, sizeof(drvinfo->version));
	strscpy(drvinfo->driver, unic_dev->comdev.adev->name,
		sizeof(drvinfo->driver));
	strscpy(drvinfo->bus_info, dev_name(unic_dev->comdev.adev->dev.parent),
		sizeof(drvinfo->bus_info));

	snprintf(drvinfo->fw_version, sizeof(drvinfo->fw_version), "%u.%u.%u.%u",
		 u32_get_bits(fw_version, UBASE_FW_VERSION_BYTE3_MASK),
		 u32_get_bits(fw_version, UBASE_FW_VERSION_BYTE2_MASK),
		 u32_get_bits(fw_version, UBASE_FW_VERSION_BYTE1_MASK),
		 u32_get_bits(fw_version, UBASE_FW_VERSION_BYTE0_MASK));
}

#define UNIC_ETHTOOL_RING	(ETHTOOL_RING_USE_RX_BUF_LEN | \
				 ETHTOOL_RING_USE_TX_PUSH)
#define UNIC_ETHTOOL_COALESCE	(ETHTOOL_COALESCE_USECS | \
				 ETHTOOL_COALESCE_USE_ADAPTIVE | \
				 ETHTOOL_COALESCE_MAX_FRAMES)

static const struct ethtool_ops unic_ethtool_ops = {
	.supported_ring_params = UNIC_ETHTOOL_RING,
	.cap_link_lanes_supported = true,
	.supported_coalesce_params = UNIC_ETHTOOL_COALESCE,
	.get_link = unic_get_link_status,
	.get_drvinfo = unic_get_driver_info,
	.get_fec_stats = unic_get_fec_stats,
};

void unic_set_ethtool_ops(struct net_device *netdev)
{
	netdev->ethtool_ops = &unic_ethtool_ops;
}

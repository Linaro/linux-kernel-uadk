// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved.
 *
 */

#include <linux/dcbnl.h>

#include "unic_hw.h"
#include "unic_netdev.h"
#include "unic_dcbnl.h"

static const struct dcbnl_rtnl_ops unic_dcbnl_ops = {
};

void unic_set_dcbnl_ops(struct net_device *netdev)
{
	struct unic_dev *unic_dev = netdev_priv(netdev);

	if (!unic_dev_ets_supported(unic_dev))
		return;

	netdev->dcbnl_ops = &unic_dcbnl_ops;

	unic_dev->dcbx_cap = DCB_CAP_DCBX_VER_IEEE | DCB_CAP_DCBX_HOST;
}

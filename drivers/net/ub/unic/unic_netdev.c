// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved.
 *
 */

#define dev_fmt(fmt) "unic: (pid %d) " fmt, current->pid

#include <net/addrconf.h>
#include <net/dsfield.h>
#include <linux/etherdevice.h>
#include <linux/if_arp.h>
#include <linux/if_vlan.h>
#include <linux/inetdevice.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/time.h>
#include <linux/u64_stats_sync.h>
#include <ub/ubase/ubase_comm_cmd.h>
#include <ub/ubase/ubase_comm_eq.h>

#include "unic_cmd.h"
#include "unic_dev.h"
#include "unic_hw.h"
#include "unic_netdev.h"

static int unic_net_up(struct net_device *netdev)
{
	struct unic_dev *unic_dev = netdev_priv(netdev);
	int ret;

	ret = unic_mac_mode_cfg(netdev, true);
	if (ret)
		return ret;

	unic_dev->sw_link_status = UNIC_LINK_STATUS_DOWN;

	return 0;
}

void unic_link_status_change(struct net_device *netdev, bool linkup)
{
	struct unic_dev *unic_dev = netdev_priv(netdev);

	/* stop/wake network interface by deactivate/activate event */
	if (test_bit(UNIC_STATE_DEACTIVATE, &unic_dev->state))
		goto out;

	if (linkup) {
		if (!test_bit(UNIC_STATE_TESTING, &unic_dev->state))
			netif_carrier_on(netdev);
	} else {
		netif_carrier_off(netdev);
	}

out:
	if (netif_msg_link(unic_dev))
		unic_info(unic_dev, "%s.\n", linkup ? "link up" : "link down");
}

void unic_link_status_update(struct unic_dev *unic_dev)
{
	u8 link_status = UNIC_LINK_STATUS_DOWN;
	int ret;

	if (test_and_set_bit(UNIC_STATE_LINK_UPDATING, &unic_dev->state))
		return;

	if (test_bit(UNIC_STATE_DOWN, &unic_dev->state))
		goto ifdown;

	ret = unic_query_link_status(unic_dev, &link_status);
	if (ret) {
		clear_bit(UNIC_STATE_LINK_UPDATING, &unic_dev->state);
		return;
	}

	unic_dev->hw.mac.link_status = link_status;
	if (unic_dev_ubl_supported(unic_dev))
		link_status = UNIC_LINK_STATUS_UP;

ifdown:
	if (link_status != unic_dev->sw_link_status) {
		unic_dev->sw_link_status = link_status;
		unic_link_status_change(unic_dev->comdev.netdev, link_status);
	}

	clear_bit(UNIC_STATE_LINK_UPDATING, &unic_dev->state);
}

int unic_net_open(struct net_device *netdev)
{
	struct unic_dev *unic_dev = netdev_priv(netdev);
	int ret;

	if (test_bit(UNIC_STATE_RESETTING, &unic_dev->state))
		return -EBUSY;

	if (!test_bit(UNIC_STATE_DOWN, &unic_dev->state)) {
		unic_warn(unic_dev, "net open repeatedly.\n");
		return 0;
	}

	netif_carrier_off(netdev);

	/* only cfg mac mode, wake network interface by activate event */
	if (test_bit(UNIC_STATE_DEACTIVATE, &unic_dev->state)) {
		ret = unic_mac_mode_cfg(netdev, true);
		if (ret) {
			unic_err(unic_dev, "failed to cfg mac mode, ret = %d.\n",
				 ret);
			return ret;
		}

		clear_bit(UNIC_STATE_DOWN, &unic_dev->state);
		return 0;
	}

	ret = unic_net_up(netdev);
	if (ret) {
		unic_err(unic_dev, "failed to up net, ret = %d.\n", ret);
		return ret;
	}

	clear_bit(UNIC_STATE_DOWN, &unic_dev->state);

	if (netif_msg_ifup(unic_dev))
		unic_info(unic_dev, "net open.\n");

	return 0;
}

int unic_net_open_no_link_change(struct net_device *netdev)
{
	struct unic_dev *unic_dev = netdev_priv(netdev);

	if (unic_resetting(netdev))
		return -EBUSY;

	if (test_bit(UNIC_STATE_DOWN, &unic_dev->state))
		return 0;

	netif_carrier_off(netdev);

	if (netif_msg_ifup(unic_dev))
		unic_info(unic_dev, "netif open.\n");

	/* ensure that network interface is awakened when linked up,
	 * otherwise, it will be handled by periodic tasks
	 */
	if (unic_dev->sw_link_status == UNIC_LINK_STATUS_UP)
		netif_carrier_on(netdev);

	return 0;
}

static void unic_dev_stop(struct net_device *netdev)
{
	struct unic_dev *unic_dev = netdev_priv(netdev);

	unic_mac_mode_cfg(netdev, false);

	unic_link_status_update(unic_dev);
}

void unic_net_stop_no_link_change(struct net_device *netdev)
{
	struct unic_dev *unic_dev = netdev_priv(netdev);

	if (test_bit(UNIC_STATE_DOWN, &unic_dev->state))
		return;

	if (netif_msg_ifdown(unic_dev))
		unic_info(unic_dev, "netif stop.\n");

	netif_carrier_off(netdev);
	netif_tx_disable(netdev);
}

int unic_net_stop(struct net_device *netdev)
{
	struct unic_dev *unic_dev = netdev_priv(netdev);

	if (test_and_set_bit(UNIC_STATE_DOWN, &unic_dev->state))
		return 0;

	/* only cfg mac mode, because stop network interface has been done
	 * by deactivate event
	 */
	if (test_bit(UNIC_STATE_DEACTIVATE, &unic_dev->state)) {
		unic_dev_stop(netdev);
		return 0;
	}

	if (netif_msg_ifdown(unic_dev))
		unic_info(unic_dev, "net stop.\n");

	netif_carrier_off(netdev);
	netif_tx_disable(netdev);
	unic_dev_stop(netdev);

	return 0;
}

static const struct net_device_ops unic_netdev_ops = {
	.ndo_open = unic_net_open,
	.ndo_stop = unic_net_stop,
};

void unic_set_netdev_ops(struct net_device *netdev)
{
	netdev->netdev_ops = &unic_netdev_ops;
}

int unic_query_link_status(struct unic_dev *unic_dev, u8 *link_status)
{
	struct unic_link_status_cmd_resp resp = {0};
	struct ubase_cmd_buf out;
	struct ubase_cmd_buf in;
	int ret;

	ubase_fill_inout_buf(&in, UBASE_OPC_QUERY_LINK_STATUS, true, 0, NULL);

	ubase_fill_inout_buf(&out, UBASE_OPC_QUERY_LINK_STATUS, false,
			     sizeof(resp), &resp);

	ret = ubase_cmd_send_inout(unic_dev->comdev.adev, &in, &out);
	if (ret) {
		unic_err(unic_dev,
			 "failed to get mac link status, ret = %d.\n", ret);
		return ret;
	}

	*link_status = (resp.status & UNIC_LINK_STATUS_UP_M) > 0 ?
		       UNIC_LINK_STATUS_UP : UNIC_LINK_STATUS_DOWN;

	return 0;
}

// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved.
 *
 */

#define dev_fmt(fmt) "unic: (pid %d) " fmt, current->pid

#include <linux/spinlock.h>
#ifdef CONFIG_UB_UNIC_UBL
#include <net/ub/ubl.h>
#endif
#include <ub/ubase/ubase_comm_cmd.h>

#include "unic_guid.h"

#ifndef UBL_ALEN
#define UBL_ALEN 16
#endif

static bool unic_is_valid_net_guid(u8 *guid)
{
#define UNIC_MGUID_PREFIX_LEN 14

	u8 invalid_guid[UBL_ALEN];

	memset(invalid_guid, 0, UBL_ALEN);
	if (!memcmp(guid, invalid_guid, UBL_ALEN))
		return false;

	memset(invalid_guid, 0xff, UBL_ALEN);
	if (!memcmp(guid, invalid_guid, UNIC_MGUID_PREFIX_LEN))
		return false;

	return true;
}

static inline void unic_guid_le_to_net_trans(u8 *src_guid, u8 *dest_guid)
{
	int i;

	for (i = 0; i < UBL_ALEN; i++)
		dest_guid[i] = src_guid[UBL_ALEN - i - 1];
}

static void unic_query_net_guid(struct unic_dev *unic_dev, u8 *guid)
{
	struct unic_query_net_guid_cmd resp = {0};
	struct ubase_cmd_buf in, out;
	int ret;

	ubase_fill_inout_buf(&in, UBASE_OPC_QUERY_NET_GUID, true, 0, NULL);
	ubase_fill_inout_buf(&out, UBASE_OPC_QUERY_NET_GUID, true,
			     sizeof(resp), &resp);
	ret = ubase_cmd_send_inout(unic_dev->comdev.adev, &in, &out);
	if (ret) {
		dev_warn(unic_dev->comdev.adev->dev.parent,
			 "failed to query net guid, ret = %d.\n", ret);
		return;
	}

	memcpy(guid, resp.guid, UBL_ALEN);
}

static void unic_get_net_guid(struct unic_dev *unic_dev, u8 *guid)
{
	struct auxiliary_device *adev = unic_dev->comdev.adev;
	u8 out_guid[UBL_ALEN] = {0};
	bool is_random = false;

	unic_query_net_guid(unic_dev, out_guid);

	unic_guid_le_to_net_trans(out_guid, guid);

	while (unlikely(!unic_is_valid_net_guid(guid))) {
		get_random_bytes(guid, UBL_ALEN);
		is_random = true;
	}

	if (unlikely(is_random))
		dev_warn(adev->dev.parent,
			 "using random GUID %02x:%02x:...:%02x:%02x.\n",
			 guid[0], guid[1],
			 guid[UBL_ALEN - 2], guid[UBL_ALEN - 1]);
}

int unic_init_guid(struct unic_dev *unic_dev)
{
	struct net_device *netdev = unic_dev->comdev.netdev;
	u8 guid[UBL_ALEN];

	unic_get_net_guid(unic_dev, guid);

	dev_addr_set(netdev, guid);
	memcpy(netdev->perm_addr, guid, netdev->addr_len);

	return 0;
}

// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved.
 *
 */

#include <ub/ubase/ubase_comm_cmd.h>

#include "unic_cmd.h"
#include "unic_dev.h"
#include "unic_hw.h"
#include "unic_netdev.h"
#include "unic_crq.h"

static void __unic_handle_link_status_event(struct auxiliary_device *adev,
					    u8 hw_link_status)
{
	struct unic_dev *unic_dev = dev_get_drvdata(&adev->dev);
	u8 link_status;

	if (test_and_set_bit(UNIC_STATE_LINK_UPDATING, &unic_dev->state))
		return;

	unic_dbg(unic_dev, "HW link_event status = %u.\n", hw_link_status);

	unic_dev->hw.mac.link_status = hw_link_status;
	if (unic_dev_ubl_supported(unic_dev))
		hw_link_status = UNIC_LINK_STATUS_UP;

	link_status = test_bit(UNIC_STATE_DOWN,
			       &unic_dev->state) ? 0 : hw_link_status;
	if (link_status != unic_dev->sw_link_status) {
		unic_dev->sw_link_status = link_status;
		unic_link_status_change(unic_dev->comdev.netdev, link_status);
	}

	clear_bit(UNIC_STATE_LINK_UPDATING, &unic_dev->state);
}

int unic_handle_link_status_event(void *dev, void *data, u32 len)
{
	struct unic_link_status_cmd_resp *resp = data;
	struct auxiliary_device *adev = dev;
	u8 hw_link_status = resp->status;

	__unic_handle_link_status_event(adev, hw_link_status);

	return 0;
}

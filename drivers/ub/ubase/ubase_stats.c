// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved.
 *
 */

#include "ubase_cmd.h"
#include "ubase_stats.h"

static int ubase_update_mac_stats(struct ubase_dev *udev, u16 port_id, u64 *data,
				  u16 size, bool is_accumulate)
{
	u16 mac_stats_num = udev->caps.dev_caps.mac_stats_num;
	struct ubase_query_mac_stats_cmd *cmd;
	struct ubase_cmd_buf in, out;
	u16 i, cmd_size;
	int ret;

	if (!ubase_dev_mac_stats_supported(udev)) {
		ubase_err(udev, "not support get mac stats.\n");
		return -EOPNOTSUPP;
	}

	cmd_size = mac_stats_num * sizeof(u64) + sizeof(*cmd);
	cmd = kzalloc(cmd_size, GFP_KERNEL);
	if (!cmd) {
		ubase_err(udev, "failed to alloc cmdq out_regs.\n");
		return -ENOMEM;
	}

	cmd->port_id = cpu_to_le16(port_id);
	ubase_fill_inout_buf(&in, UBASE_OPC_STATS_MAC_ALL, true, cmd_size, cmd);
	ubase_fill_inout_buf(&out, UBASE_OPC_STATS_MAC_ALL, true, cmd_size, cmd);
	ret = __ubase_cmd_send_inout(udev, &in, &out);
	if (ret) {
		ubase_err(udev, "failed to get mac stats, ret = %d.\n", ret);
		goto out_send_cmd_fail;
	}

	mac_stats_num = min_t(u16, size, mac_stats_num);
	if (is_accumulate)
		for (i = 0; i < mac_stats_num; i++)
			*data++ += le64_to_cpu(cmd->stats_val[i]);
	else
		for (i = 0; i < mac_stats_num; i++)
			*data++ = le64_to_cpu(cmd->stats_val[i]);

out_send_cmd_fail:
	kfree(cmd);

	return ret;
}

int ubase_get_ub_port_stats(struct auxiliary_device *adev, u16 port_id,
			    struct ubase_ub_dl_stats *data)
{
	struct ubase_dev *udev;

	if (!adev || !data)
		return -EINVAL;

	udev = __ubase_get_udev_by_adev(adev);

	return ubase_update_mac_stats(udev, port_id, (u64 *)data,
				      sizeof(*data) / sizeof(u64), false);
}
EXPORT_SYMBOL(ubase_get_ub_port_stats);

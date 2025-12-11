// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved.
 *
 */

#define dev_fmt(fmt) "unic: (pid %d) " fmt, current->pid

#include "unic_hw.h"
#include "unic_qos_hw.h"

int unic_set_hw_vl_map(struct unic_dev *unic_dev, u8 *dscp_vl, u8 *prio_vl,
		       u8 map_type)
{
	struct auxiliary_device *adev = unic_dev->comdev.adev;
	struct unic_config_vl_map_cmd req = {0};
	struct ubase_cmd_buf in;
	int ret;

	memcpy(req.dscp_vl, dscp_vl, sizeof(req.dscp_vl));
	memcpy(req.prio_vl, prio_vl, sizeof(req.prio_vl));
	req.map_type = map_type;
	ubase_fill_inout_buf(&in, UBASE_OPC_CFG_VL_MAP, false,
			     sizeof(req), &req);
	ret = ubase_cmd_send_in(adev, &in);
	if (ret)
		dev_err(adev->dev.parent, "failed to set vl map. ret = %d.\n", ret);

	return ret;
}

/* vl_maxrate: byte per second */
int unic_config_vl_rate_limit(struct unic_dev *unic_dev, u64 *vl_maxrate,
			      u16 vl_bitmap)
{
	struct auxiliary_device *adev = unic_dev->comdev.adev;
	struct ubase_caps *caps = ubase_get_dev_caps(adev);
	u32 max_speed = unic_dev->hw.mac.max_speed;
	struct unic_config_vl_speed_cmd req = {0};
	struct ubase_cmd_buf in;
	u32 vl_rate;
	int i, ret;

	req.bus_ue_id = cpu_to_le16(USHRT_MAX);
	req.vl_bitmap = cpu_to_le16(vl_bitmap);
	for (i = 0; i < caps->vl_num; i++) {
		vl_rate = vl_maxrate[i] / UNIC_MBYTE_PER_SEND;
		vl_rate = vl_rate ? vl_rate : max_speed;
		req.max_speed[caps->req_vl[i]] = cpu_to_le32(vl_rate);
		req.max_speed[caps->resp_vl[i]] = cpu_to_le32(vl_rate);
	}

	ubase_fill_inout_buf(&in, UBASE_OPC_VL_RATE_LIMIT_CONFIG, false,
			     sizeof(req), &req);

	ret = ubase_cmd_send_in(adev, &in);
	if (ret)
		dev_err(adev->dev.parent,
			"failed to config vl rate limit, ret = %d.\n", ret);

	return ret;
}

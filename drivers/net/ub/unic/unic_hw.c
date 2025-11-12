// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved.
 *
 */

#define dev_fmt(fmt) "unic: (pid %d) " fmt, current->pid

#include <linux/phy.h>
#include <ub/ubase/ubase_comm_cmd.h>

#include "unic.h"
#include "unic_cmd.h"
#include "unic_dev.h"
#include "unic_hw.h"

static const struct unic_speed_bit_map speed_bit_map[] = {
	/* caution: should keep them in descending order by speed!
	 * see unic_get_max_speed()
	 */
	{UNIC_MAC_SPEED_400G, UNIC_LANES_8, UNIC_SUPPORT_400G_X8_BIT},
	{UNIC_MAC_SPEED_400G, UNIC_LANES_4, UNIC_SUPPORT_400G_X4_BIT},
	{UNIC_MAC_SPEED_200G, UNIC_LANES_4, UNIC_SUPPORT_200G_X4_BIT},
	{UNIC_MAC_SPEED_200G, UNIC_LANES_2, UNIC_SUPPORT_200G_X2_BIT},
	{UNIC_MAC_SPEED_100G, UNIC_LANES_4, UNIC_SUPPORT_100G_X4_BIT},
	{UNIC_MAC_SPEED_100G, UNIC_LANES_2, UNIC_SUPPORT_100G_X2_BIT},
	{UNIC_MAC_SPEED_100G, UNIC_LANES_1, UNIC_SUPPORT_100G_X1_BIT},
	{UNIC_MAC_SPEED_50G, UNIC_LANES_2, UNIC_SUPPORT_50G_X2_BIT},
	{UNIC_MAC_SPEED_50G, UNIC_LANES_1, UNIC_SUPPORT_50G_X1_BIT},
	{UNIC_MAC_SPEED_25G, UNIC_LANES_1, UNIC_SUPPORT_25G_X1_BIT},
	{UNIC_MAC_SPEED_10G, UNIC_LANES_1, UNIC_SUPPORT_10G_X1_BIT},
};

static int unic_get_port_info(struct unic_dev *unic_dev)
{
	struct unic_query_port_info_resp resp = {0};
	struct unic_mac *mac = &unic_dev->hw.mac;
	struct ubase_cmd_buf in, out;
	int ret;

	ubase_fill_inout_buf(&in, UBASE_OPC_QUERY_PORT_INFO, true, 0, NULL);
	ubase_fill_inout_buf(&out, UBASE_OPC_QUERY_PORT_INFO, false,
			     sizeof(resp), &resp);

	ret = ubase_cmd_send_inout(unic_dev->comdev.adev, &in, &out);
	if (ret)
		return ret;

	mac->speed = le32_to_cpu(resp.speed);
	mac->speed_ability = le32_to_cpu(resp.speed_ability);
	mac->autoneg = resp.autoneg;
	mac->support_autoneg = resp.autoneg_ability;
	mac->lanes = resp.lanes;
	mac->module_type = resp.module_type;
	mac->fec_mode = le16_to_cpu(resp.fec_mode);
	mac->fec_ability = le16_to_cpu(resp.fec_ability);

	return 0;
}

int unic_set_mac_autoneg(struct unic_dev *unic_dev, u8 autoneg)
{
	struct unic_cfg_autoneg_mode_cmd req = {0};
	struct ubase_cmd_buf in;
	int ret;

	if (unic_dev_ubl_supported(unic_dev))
		return unic_is_initing_or_resetting(unic_dev) ? 0 : -EOPNOTSUPP;

	if (!unic_dev->hw.mac.support_autoneg)
		return 0;

	req.autoneg_en = autoneg ? 1 : 0;

	ubase_fill_inout_buf(&in, UBASE_OPC_CONFIG_AUTONEG_MODE, false,
			     sizeof(req), &req);

	ret = ubase_cmd_send_in(unic_dev->comdev.adev, &in);
	if (ret && ret != -EPERM)
		dev_err(unic_dev->comdev.adev->dev.parent,
			"failed to send cmd in config autoneg(%u), ret = %d.\n",
			autoneg, ret);

	if (!ret)
		unic_dev->hw.mac.autoneg = req.autoneg_en;

	return ret;
}

int unic_set_mac_speed_duplex(struct unic_dev *unic_dev, u32 speed, u8 duplex,
			      u8 lanes)
{
	struct unic_cfg_speed_dup_cmd req = {0};
	struct ubase_cmd_buf in;
	int ret;

	if (unic_dev_ubl_supported(unic_dev))
		return unic_is_initing_or_resetting(unic_dev) ? 0 : -EOPNOTSUPP;

	req.speed = cpu_to_le32(speed);
	req.duplex = duplex;
	req.lanes = lanes;

	ubase_fill_inout_buf(&in, UBASE_OPC_CONFIG_SPEED_DUP, false,
			     sizeof(req), &req);

	ret = ubase_cmd_send_in(unic_dev->comdev.adev, &in);
	if (ret && ret != -EPERM)
		dev_err(unic_dev->comdev.adev->dev.parent,
			"failed to send cmd in config speed(%u), ret = %d.\n",
			speed, ret);

	return ret;
}

static void unic_set_fec_ability(struct unic_mac *mac)
{
	linkmode_clear_bit(ETHTOOL_LINK_MODE_FEC_NONE_BIT, mac->supported);
	linkmode_clear_bit(ETHTOOL_LINK_MODE_FEC_RS_BIT, mac->supported);
	linkmode_clear_bit(ETHTOOL_LINK_MODE_FEC_BASER_BIT, mac->supported);
	linkmode_clear_bit(ETHTOOL_LINK_MODE_FEC_LLRS_BIT, mac->supported);

	if (mac->fec_ability & ETHTOOL_FEC_RS)
		linkmode_set_bit(ETHTOOL_LINK_MODE_FEC_RS_BIT,
				 mac->supported);
	if (mac->fec_ability & ETHTOOL_FEC_OFF)
		linkmode_set_bit(ETHTOOL_LINK_MODE_FEC_NONE_BIT,
				 mac->supported);
}

static const struct unic_link_mode_bit_map sr_linkmode_bit_map[] = {
	{UNIC_SUPPORT_400G_X8_BIT, ETHTOOL_LINK_MODE_400000baseSR8_Full_BIT},
	{UNIC_SUPPORT_400G_X4_BIT, ETHTOOL_LINK_MODE_400000baseSR4_Full_BIT},
	{UNIC_SUPPORT_200G_X4_BIT, ETHTOOL_LINK_MODE_200000baseSR4_Full_BIT},
	{UNIC_SUPPORT_200G_X2_BIT, ETHTOOL_LINK_MODE_200000baseSR2_Full_BIT},
	{UNIC_SUPPORT_100G_X4_BIT, ETHTOOL_LINK_MODE_100000baseSR4_Full_BIT},
	{UNIC_SUPPORT_100G_X2_BIT, ETHTOOL_LINK_MODE_100000baseSR2_Full_BIT},
	{UNIC_SUPPORT_100G_X1_BIT, ETHTOOL_LINK_MODE_100000baseSR_Full_BIT},
	{UNIC_SUPPORT_50G_X2_BIT, ETHTOOL_LINK_MODE_50000baseSR2_Full_BIT},
	{UNIC_SUPPORT_50G_X1_BIT, ETHTOOL_LINK_MODE_50000baseSR_Full_BIT},
	{UNIC_SUPPORT_25G_X1_BIT, ETHTOOL_LINK_MODE_25000baseSR_Full_BIT},
	{UNIC_SUPPORT_10G_X1_BIT, ETHTOOL_LINK_MODE_10000baseSR_Full_BIT},
};

static const struct unic_link_mode_bit_map lr_linkmode_bit_map[] = {
	{UNIC_SUPPORT_400G_X8_BIT, ETHTOOL_LINK_MODE_400000baseLR8_ER8_FR8_Full_BIT},
	{UNIC_SUPPORT_400G_X4_BIT, ETHTOOL_LINK_MODE_400000baseLR4_ER4_FR4_Full_BIT},
	{UNIC_SUPPORT_200G_X4_BIT, ETHTOOL_LINK_MODE_200000baseLR4_ER4_FR4_Full_BIT},
	{UNIC_SUPPORT_200G_X2_BIT, ETHTOOL_LINK_MODE_200000baseLR2_ER2_FR2_Full_BIT},
	{UNIC_SUPPORT_100G_X4_BIT, ETHTOOL_LINK_MODE_100000baseLR4_ER4_Full_BIT},
	{UNIC_SUPPORT_100G_X2_BIT, ETHTOOL_LINK_MODE_100000baseLR2_ER2_FR2_Full_BIT},
	{UNIC_SUPPORT_100G_X1_BIT, ETHTOOL_LINK_MODE_100000baseLR_ER_FR_Full_BIT},
	{UNIC_SUPPORT_50G_X1_BIT, ETHTOOL_LINK_MODE_50000baseLR_ER_FR_Full_BIT},
	{UNIC_SUPPORT_10G_X1_BIT, ETHTOOL_LINK_MODE_10000baseLR_Full_BIT},
};

static const struct unic_link_mode_bit_map cr_linkmode_bit_map[] = {
	{UNIC_SUPPORT_400G_X8_BIT, ETHTOOL_LINK_MODE_400000baseCR8_Full_BIT},
	{UNIC_SUPPORT_400G_X4_BIT, ETHTOOL_LINK_MODE_400000baseCR4_Full_BIT},
	{UNIC_SUPPORT_200G_X4_BIT, ETHTOOL_LINK_MODE_200000baseCR4_Full_BIT},
	{UNIC_SUPPORT_200G_X2_BIT, ETHTOOL_LINK_MODE_200000baseCR2_Full_BIT},
	{UNIC_SUPPORT_100G_X4_BIT, ETHTOOL_LINK_MODE_100000baseCR4_Full_BIT},
	{UNIC_SUPPORT_100G_X2_BIT, ETHTOOL_LINK_MODE_100000baseCR2_Full_BIT},
	{UNIC_SUPPORT_100G_X1_BIT, ETHTOOL_LINK_MODE_100000baseCR_Full_BIT},
	{UNIC_SUPPORT_50G_X2_BIT, ETHTOOL_LINK_MODE_50000baseCR2_Full_BIT},
	{UNIC_SUPPORT_50G_X1_BIT, ETHTOOL_LINK_MODE_50000baseCR_Full_BIT},
	{UNIC_SUPPORT_25G_X1_BIT, ETHTOOL_LINK_MODE_25000baseCR_Full_BIT},
	{UNIC_SUPPORT_10G_X1_BIT, ETHTOOL_LINK_MODE_10000baseCR_Full_BIT},
};

static const struct unic_link_mode_bit_map kr_linkmode_bit_map[] = {
	{UNIC_SUPPORT_400G_X8_BIT, ETHTOOL_LINK_MODE_400000baseKR8_Full_BIT},
	{UNIC_SUPPORT_400G_X4_BIT, ETHTOOL_LINK_MODE_400000baseKR4_Full_BIT},
	{UNIC_SUPPORT_200G_X4_BIT, ETHTOOL_LINK_MODE_200000baseKR4_Full_BIT},
	{UNIC_SUPPORT_200G_X2_BIT, ETHTOOL_LINK_MODE_200000baseKR2_Full_BIT},
	{UNIC_SUPPORT_100G_X4_BIT, ETHTOOL_LINK_MODE_100000baseKR4_Full_BIT},
	{UNIC_SUPPORT_100G_X2_BIT, ETHTOOL_LINK_MODE_100000baseKR2_Full_BIT},
	{UNIC_SUPPORT_100G_X1_BIT, ETHTOOL_LINK_MODE_100000baseKR_Full_BIT},
	{UNIC_SUPPORT_50G_X2_BIT, ETHTOOL_LINK_MODE_50000baseKR2_Full_BIT},
	{UNIC_SUPPORT_50G_X1_BIT, ETHTOOL_LINK_MODE_50000baseKR_Full_BIT},
	{UNIC_SUPPORT_25G_X1_BIT, ETHTOOL_LINK_MODE_25000baseKR_Full_BIT},
	{UNIC_SUPPORT_10G_X1_BIT, ETHTOOL_LINK_MODE_10000baseKR_Full_BIT},
};

static void unic_set_linkmode(const struct unic_link_mode_bit_map *map, u32 map_size,
			      u32 speed_ability, unsigned long *link_mode)
{
	u32 i;

	for (i = 0; i < map_size; i++) {
		if (speed_ability & map[i].speed_bit)
			linkmode_set_bit(map[i].link_mode, link_mode);
	}
}

static void unic_set_linkmode_sr(u32 speed_ability, unsigned long *link_mode)
{
	unic_set_linkmode(sr_linkmode_bit_map, ARRAY_SIZE(sr_linkmode_bit_map),
			  speed_ability, link_mode);
}

static void unic_set_linkmode_lr(u32 speed_ability, unsigned long *link_mode)
{
	unic_set_linkmode(lr_linkmode_bit_map, ARRAY_SIZE(lr_linkmode_bit_map),
			  speed_ability, link_mode);
}

static void unic_set_linkmode_cr(u32 speed_ability, unsigned long *link_mode)
{
	unic_set_linkmode(cr_linkmode_bit_map, ARRAY_SIZE(cr_linkmode_bit_map),
			  speed_ability, link_mode);
}

static void unic_set_linkmode_kr(u32 speed_ability, unsigned long *link_mode)
{
	unic_set_linkmode(kr_linkmode_bit_map, ARRAY_SIZE(kr_linkmode_bit_map),
			  speed_ability, link_mode);
}

static void unic_update_speed_advertising(struct unic_mac *mac)
{
	u32 speed_ability = mac->speed_ability;

	switch (mac->module_type) {
	case UNIC_MODULE_TYPE_FIBRE_LR:
		unic_set_linkmode_lr(speed_ability, mac->advertising);
		break;
	case UNIC_MODULE_TYPE_FIBRE_SR:
	case UNIC_MODULE_TYPE_AOC:
		unic_set_linkmode_sr(speed_ability, mac->advertising);
		break;
	case UNIC_MODULE_TYPE_CR:
		unic_set_linkmode_cr(speed_ability, mac->advertising);
		break;
	case UNIC_MODULE_TYPE_KR:
		unic_set_linkmode_kr(speed_ability, mac->advertising);
		break;
	default:
		break;
	}
}

static void unic_update_fec_advertising(struct unic_mac *mac)
{
	if (mac->fec_mode & ETHTOOL_FEC_RS)
		linkmode_set_bit(ETHTOOL_LINK_MODE_FEC_RS_BIT,
				 mac->advertising);
	else if (mac->fec_mode & ETHTOOL_FEC_OFF)
		linkmode_set_bit(ETHTOOL_LINK_MODE_FEC_NONE_BIT,
				 mac->advertising);
}

static void unic_update_advertising(struct unic_dev *unic_dev)
{
	struct unic_mac *mac = &unic_dev->hw.mac;

	linkmode_zero(mac->advertising);

	unic_update_speed_advertising(mac);
	unic_update_fec_advertising(mac);
}

static void unic_update_port_capability(struct unic_dev *unic_dev)
{
	struct unic_mac *mac = &unic_dev->hw.mac;

	unic_set_fec_ability(mac);

	if (mac->support_autoneg) {
		linkmode_set_bit(ETHTOOL_LINK_MODE_Autoneg_BIT, mac->supported);
		linkmode_copy(mac->advertising, mac->supported);
	} else {
		linkmode_clear_bit(ETHTOOL_LINK_MODE_Autoneg_BIT, mac->supported);
		unic_update_advertising(unic_dev);
	}
}

int unic_update_port_info(struct unic_dev *unic_dev)
{
	int ret;

	ret = unic_get_port_info(unic_dev);
	if (ret)
		return ret;

	unic_update_port_capability(unic_dev);

	return 0;
}

static void unic_setup_promisc_req(struct unic_promisc_cfg_cmd *req,
				   struct unic_promisc_en *promisc_en)
{
	req->promisc_uc_ind = 1;
	req->promisc_rx_uc_guid_en = promisc_en->en_uc_guid;
	req->promisc_rx_uc_ip_en = promisc_en->en_uc_ip;

	req->promisc_mc_ind = 1;
	req->promisc_rx_mc_en = promisc_en->en_mc;
}

int unic_set_promisc_mode(struct unic_dev *unic_dev,
			  struct unic_promisc_en *promisc_en)
{
	struct auxiliary_device *adev = unic_dev->comdev.adev;
	struct unic_promisc_cfg_cmd req = {0};
	struct ubase_cmd_buf in;
	u32 time_out;
	int ret;

	unic_setup_promisc_req(&req, promisc_en);

	ubase_fill_inout_buf(&in, UBASE_OPC_CFG_PROMISC_MODE, false,
			     sizeof(req), &req);

	time_out = unic_cmd_timeout(unic_dev);
	ret = ubase_cmd_send_in_ex(adev, &in, time_out);
	if (ret == -EPERM)
		return 0;
	else if (ret)
		unic_err(unic_dev,
			 "failed to set promisc mode, ret = %d.\n", ret);

	return ret;
}

void unic_fill_promisc_en(struct unic_promisc_en *promisc_en, u8 flags)
{
	promisc_en->en_uc_ip = !!(flags & UNIC_UPE);
	promisc_en->en_mc = !!(flags & UNIC_MPE);
}

int unic_activate_promisc_mode(struct unic_dev *unic_dev, bool activate)
{
	struct unic_promisc_en promisc_en = {0};
	u8 flags;

	flags = unic_dev->netdev_flags | unic_dev->vport.last_promisc_flags;
	if (!flags)
		return 0;

	if (activate)
		unic_fill_promisc_en(&promisc_en, flags);

	return unic_set_promisc_mode(unic_dev, &promisc_en);
}

int unic_sync_promisc_mode(struct unic_dev *unic_dev)
{
	struct unic_act_info *act_info = &unic_dev->act_info;
	struct unic_vport *vport = &unic_dev->vport;
	struct unic_promisc_en promisc_en;
	struct auxiliary_device *adev;
	int ret = 0;

	adev = unic_dev->comdev.adev;

	if (!mutex_trylock(&act_info->mutex))
		return 0;

	if (act_info->deactivate)
		goto out;

	if (vport->last_promisc_flags != vport->overflow_promisc_flags) {
		set_bit(UNIC_VPORT_STATE_PROMISC_CHANGE, &vport->state);
		vport->last_promisc_flags = vport->overflow_promisc_flags;
	}

	if (!test_and_clear_bit(UNIC_VPORT_STATE_PROMISC_CHANGE, &vport->state))
		goto out;

	unic_fill_promisc_en(&promisc_en,
			     unic_dev->netdev_flags | vport->last_promisc_flags);

	ret = unic_set_promisc_mode(unic_dev, &promisc_en);
	if (ret)
		set_bit(UNIC_VPORT_STATE_PROMISC_CHANGE, &vport->state);

out:
	mutex_unlock(&act_info->mutex);

	return ret;
}

static void unic_parse_fiber_link_mode(struct unic_dev *unic_dev,
				       u32 speed_ability)
{
	struct unic_mac *mac = &unic_dev->hw.mac;

	unic_set_linkmode_sr(speed_ability, mac->supported);
	unic_set_linkmode_lr(speed_ability, mac->supported);
	unic_set_linkmode_cr(speed_ability, mac->supported);

	linkmode_set_bit(ETHTOOL_LINK_MODE_FIBRE_BIT, mac->supported);
	linkmode_set_bit(ETHTOOL_LINK_MODE_FEC_NONE_BIT, mac->supported);
}

static void unic_parse_backplane_link_mode(struct unic_dev *unic_dev,
					   u32 speed_ability)
{
	struct unic_mac *mac = &unic_dev->hw.mac;

	unic_set_linkmode_kr(speed_ability, mac->supported);

	linkmode_set_bit(ETHTOOL_LINK_MODE_Backplane_BIT, mac->supported);
	linkmode_set_bit(ETHTOOL_LINK_MODE_FEC_NONE_BIT, mac->supported);
}

static void unic_parse_link_mode(struct unic_dev *unic_dev, u32 speed_ability)
{
	u8 media_type = unic_dev->hw.mac.media_type;

	if (media_type == UNIC_MEDIA_TYPE_FIBER)
		unic_parse_fiber_link_mode(unic_dev, speed_ability);
	else if (media_type == UNIC_MEDIA_TYPE_BACKPLANE)
		unic_parse_backplane_link_mode(unic_dev, speed_ability);
	else
		dev_warn(unic_dev->comdev.adev->dev.parent,
			 "unknown media_type = %u.\n", media_type);
}

static u32 unic_get_max_speed(u32 cur_speed, u32 speed_ability)
{
	u32 i;

	for (i = 0; i < ARRAY_SIZE(speed_bit_map); i++) {
		if (speed_ability & speed_bit_map[i].speed_bit)
			return speed_bit_map[i].speed;
	}

	return cur_speed;
}

static void unic_parse_mac_cfg(struct unic_dev *unic_dev,
			       const struct unic_res_cmd_resp *resp)
{
	struct unic_mac *mac = &unic_dev->hw.mac;

	mac->media_type = resp->media_type;
	mac->speed = le32_to_cpu(resp->default_speed);
	mac->lanes = resp->default_lanes;
	mac->speed_ability = le32_to_cpu(resp->speed_ability);

	unic_parse_link_mode(unic_dev, mac->speed_ability);

	mac->max_speed = unic_get_max_speed(mac->speed, mac->speed_ability);

	unic_dbg(unic_dev,
		 "default speed = %u, lanes = %u, speed_ability = 0x%x\n",
		 mac->speed, mac->lanes, mac->speed_ability);
}

static void unic_parse_dev_caps(struct unic_dev *unic_dev,
				const struct unic_res_cmd_resp *resp)
{
#define KB 1024U

	struct unic_caps *caps = &unic_dev->caps;
	int i;

	for (i = 0; i < UNIC_CAP_LEN; i++)
		unic_dev->cap_bits[i] = le32_to_cpu(resp->cap_bits[i]);

	caps->rx_buff_len = le16_to_cpu(resp->rx_buff_len);
	caps->total_ip_tbl_size = le16_to_cpu(resp->total_ip_tbl_size);
	caps->max_trans_unit = le16_to_cpu(resp->max_trans_unit);
	caps->min_trans_unit = le16_to_cpu(resp->min_trans_unit);
	caps->vport_buf_size = le16_to_cpu(resp->vport_buf_size) * KB;
	caps->vport_buf_num = resp->vport_buf_num;
	caps->max_int_ql = le16_to_cpu(resp->max_int_ql);
	caps->max_int_gl = le16_to_cpu(resp->max_int_gl);

	unic_parse_mac_cfg(unic_dev, resp);
}

int unic_query_dev_res(struct unic_dev *unic_dev)
{
	struct auxiliary_device *adev = unic_dev->comdev.adev;
	struct unic_res_cmd_resp resp;
	struct ubase_cmd_buf out;
	struct ubase_cmd_buf in;
	int ret;

	memset(&resp, 0, sizeof(resp));
	ubase_fill_inout_buf(&in, UBASE_OPC_QUERY_NIC_RSRC_PARAM, true, 0,
			     NULL);

	ubase_fill_inout_buf(&out, UBASE_OPC_QUERY_NIC_RSRC_PARAM, false,
			     sizeof(resp), &resp);

	ret = ubase_cmd_send_inout(adev, &in, &out);
	if (ret) {
		dev_err(adev->dev.parent, "failed to query unic res, ret = %d.\n",
			ret);
		return ret;
	}

	unic_parse_dev_caps(unic_dev, &resp);

	return 0;
}

int unic_config_mtu(struct unic_dev *unic_dev, int new_mtu)
{
	struct auxiliary_device *adev = unic_dev->comdev.adev;
	struct unic_config_max_trans_unit_cmd req = {0};
	struct ubase_cmd_buf in;

	req.max_trans_unit = cpu_to_le16(new_mtu);

	ubase_fill_inout_buf(&in, UBASE_OPC_CFG_MTU, false, sizeof(req), &req);

	return ubase_cmd_send_in(adev, &in);
}

static int unic_query_flush_status(struct unic_dev *unic_dev, u8 *status)
{
	struct unic_query_flush_status_resp resp = {0};
	struct ubase_cmd_buf out;
	struct ubase_cmd_buf in;
	int ret;

	ubase_fill_inout_buf(&in, UBASE_OPC_QUERY_FLUSH_STATUS, true, 0, NULL);
	ubase_fill_inout_buf(&out, UBASE_OPC_QUERY_FLUSH_STATUS, false,
			     sizeof(resp), &resp);

	ret = ubase_cmd_send_inout(unic_dev->comdev.adev, &in, &out);
	if (ret) {
		if (ret == -EPERM)
			return -EOPNOTSUPP;

		unic_err(unic_dev,
			 "failed to send cmd when query flush status, ret = %d.\n",
			 ret);
		return ret;
	}

	*status = resp.status;
	return ret;
}

static int unic_wait_hw_queue_flush_done(struct unic_dev *unic_dev)
{
#define UNIC_FLUSH_STATUS_WAIT_CNT	10
#define UNIC_FLUSH_STATUS_WAIT_MS	50
#define UNIC_FLUSH_DONE			1

	int i, ret;
	u8 status;

	for (i = 0; i < UNIC_FLUSH_STATUS_WAIT_CNT; i++) {
		ret = unic_query_flush_status(unic_dev, &status);
		if (ret) {
			msleep(UNIC_FLUSH_STATUS_WAIT_MS);
			continue;
		}

		if (status == UNIC_FLUSH_DONE)
			return 0;

		msleep(UNIC_FLUSH_STATUS_WAIT_MS);
	}

	return -EBUSY;
}

int unic_mac_cfg(struct unic_dev *unic_dev, bool enable)
{
	struct unic_ld_config_mode_cmd req = {0};
	unsigned long dl_conf_mode_en = 0;
	struct ubase_cmd_buf in;
	u32 time_out;
	int ret;

	if (enable) {
		set_bit(UNIC_DL_CONFIG_MODE_TX_EN_B, &dl_conf_mode_en);
		set_bit(UNIC_DL_CONFIG_MODE_RX_EN_B, &dl_conf_mode_en);
	}

	if (test_bit(UNIC_STATE_TESTING, &unic_dev->state) && enable)
		set_bit(UNIC_DL_CONFIG_MODE_APP_LP_EN_B, &dl_conf_mode_en);

	req.txrx_en = cpu_to_le32((u32)dl_conf_mode_en);

	ubase_fill_inout_buf(&in, UBASE_OPC_DL_CONFIG_MODE, false,
			     sizeof(req), &req);

	time_out = unic_cmd_timeout(unic_dev);
	ret = ubase_cmd_send_in_ex(unic_dev->comdev.adev, &in, time_out);
	if (ret)
		unic_err(unic_dev, "failed to %s mac, ret = %d.\n",
			 enable ? "enable" : "disable", ret);

	return ret;
}

int unic_mac_mode_cfg(struct net_device *netdev, bool enable)
{
	struct unic_dev *unic_dev = netdev_priv(netdev);
	int ret = 0;

	if (!unic_dev_ubl_supported(unic_dev))
		ret = unic_mac_cfg(unic_dev, enable);

	if (!enable && !unic_resetting(netdev))
		return unic_wait_hw_queue_flush_done(unic_dev);

	return ret;
}

int unic_cfg_vport_buf(struct unic_dev *unic_dev, bool init)
{
	struct unic_cfg_vport_buf_cmd req = {0};
	struct ubase_cmd_buf in;
	dma_addr_t addr;
	u32 time_out;
	int ret, i;

	req.buf_num = unic_dev->caps.vport_buf_num;
	if (init) {
		for (i = 0; i < unic_dev->caps.vport_buf_num; i++) {
			addr = unic_dev->vbuf[i].dma_addr;
			req.buf_addr[i * U32S_PER_U64] = cpu_to_le32(lower_32_bits(addr));
			req.buf_addr[i * U32S_PER_U64 + 1] = cpu_to_le32(upper_32_bits(addr));
		}
	}

	ubase_fill_inout_buf(&in, UBASE_OPC_CFG_VPORT_BUF, false,
			     sizeof(req), &req);
	time_out = unic_cmd_timeout(unic_dev);
	ret = ubase_cmd_send_in_ex(unic_dev->comdev.adev, &in, time_out);
	if (ret)
		dev_err(unic_dev->comdev.adev->dev.parent,
			"failed to config vport buffer, ret = %d.\n", ret);
	return ret;
}

int unic_set_fec_mode(struct unic_dev *unic_dev, u32 fec_mode)
{
	struct unic_cfg_fec_cmd req = {0};
	struct ubase_cmd_buf in;
	int ret;

	req.fec_mode = cpu_to_le32(fec_mode);
	ubase_fill_inout_buf(&in, UBASE_OPC_CONFIG_FEC_MODE, false,
			     sizeof(req), &req);
	ret = ubase_cmd_send_in(unic_dev->comdev.adev, &in);
	if (ret)
		dev_err(unic_dev->comdev.adev->dev.parent,
			"failed to set fec mode(%u), ret = %d.\n",
			fec_mode, ret);

	return ret;
}

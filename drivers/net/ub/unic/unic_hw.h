/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved.
 *
 */

#ifndef __UNIC_HW_H__
#define __UNIC_HW_H__

#include <linux/ethtool.h>
#include <ub/ubase/ubase_comm_cmd.h>

#include "unic_dev.h"

#define UNIC_DL_CONFIG_MODE_TX_EN_B	0
#define UNIC_DL_CONFIG_MODE_RX_EN_B	1
#define UNIC_DL_CONFIG_MODE_APP_LP_EN_B	2

#define UNIC_LINK_STATUS_UP_B	0
#define UNIC_LINK_STATUS_UP_M	BIT(UNIC_LINK_STATUS_UP_B)

#define UNIC_LINK_STATUS_DOWN	0
#define UNIC_LINK_STATUS_UP	1

struct unic_caps_item {
	void		*p;
	u32		default_val;
	u8		size;
	const char	*name;
};

struct unic_speed_bit_map {
	u32 speed;
	u32 lanes;
	u32 speed_bit;
};

struct unic_link_mode_bit_map {
	u32 speed_bit;
	enum ethtool_link_mode_bit_indices link_mode;
};

struct unic_promisc_en {
	u8 en_uc_ip;
	u8 en_uc_guid;
	u8 en_mc;
};

static inline bool unic_is_port_down(struct unic_dev *unic_dev)
{
	return unic_dev->hw.mac.link_status == UNIC_LINK_STATUS_DOWN;
}

int unic_update_port_info(struct unic_dev *unic_dev);

int unic_set_mac_speed_duplex(struct unic_dev *unic_dev, u32 speed, u8 duplex,
			      u8 lanes);
int unic_set_mac_autoneg(struct unic_dev *unic_dev, u8 autoneg);

int unic_query_dev_res(struct unic_dev *unic_dev);

int unic_check_validate_dump_mtu(struct unic_dev *unic_dev, int new_mtu,
				 u16 *max_frame_size);
int unic_config_mtu(struct unic_dev *unic_dev, int new_mtu);

int unic_mac_cfg(struct unic_dev *unic_dev, bool enable);
int unic_mac_mode_cfg(struct net_device *netdev, bool enable);

int unic_sync_promisc_mode(struct unic_dev *unic_dev);
int unic_activate_promisc_mode(struct unic_dev *unic_dev, bool activate);
void unic_fill_promisc_en(struct unic_promisc_en *promisc_en, u8 flags);
int unic_get_promisc_mode(struct unic_dev *unic_dev,
			  struct unic_promisc_cfg_cmd *resp);
int unic_set_promisc_mode(struct unic_dev *unic_dev,
			  struct unic_promisc_en *promisc_en);

int unic_cfg_vport_buf(struct unic_dev *unic_dev, bool init);
int unic_set_fec_mode(struct unic_dev *unic_dev, u32 fec_mode);
int unic_update_fec_stats(struct unic_dev *unic_dev);

#endif

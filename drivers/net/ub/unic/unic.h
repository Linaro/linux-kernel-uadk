/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved.
 *
 */

#ifndef __UNIC_H__
#define __UNIC_H__

#include <linux/bitmap.h>
#include <linux/dcbnl.h>
#include <linux/ethtool.h>
#include <ub/ubase/ubase_comm_dev.h>

#define UNIC_CAP_LEN		3
#define UNIC_MAX_VPORT_BUF_NUM	6

enum {
	UNIC_SUPPORT_PFC_B		= 0,
	UNIC_SUPPORT_ETS_B		= 1,
	UNIC_SUPPORT_FEC_B		= 2,
	UNIC_SUPPORT_PAUSE_B		= 3,
	UNIC_SUPPORT_GRO_B		= 5,
	UNIC_SUPPORT_ETH_B		= 7,

	UNIC_SUPPORT_TSO_B			= 8,
	UNIC_SUPPORT_RSS_B			= 9,
	UNIC_SUPPORT_SERIAL_SERDES_LB_B		= 10,
	UNIC_SUPPORT_TC_SPEED_LIMIT_B		= 12,
	UNIC_SUPPORT_TX_CSUM_OFFLOAD_B		= 13,
	UNIC_SUPPORT_TUNNEL_CSUM_OFFLOAD_B	= 14,
	UNIC_SUPPORT_PTP_B			= 15,

	UNIC_SUPPORT_RX_CSUM_OFFLOAD_B		= 16,
	UNIC_SUPPORT_APP_LB_B			= 17,
	UNIC_SUPPORT_FEC_STATS_B		= 21,
	UNIC_SUPPORT_EXTERNAL_LB_B		= 23,

	UNIC_SUPPORT_PARALLEL_SERDES_LB_B	= 24,
	UNIC_SUPPORT_CFG_VLAN_FILTER_B		= 26,
	UNIC_SUPPORT_CFG_MAC_B			= 27,

	/* must be last entry and it should <= UNIC_CAP_LEN * 32 */
	UNIC_SUPPORT_MASK_NBITS
};

#define unic_get_cap_bit(unic_dev, nr) \
	test_bit(nr, (unsigned long *)((unic_dev)->cap_bits))

#define UNIC_USER_UPE				BIT(0) /* unicast promisc enabled by user */
#define UNIC_USER_MPE				BIT(1) /* mulitcast promisc enabled by user */
#define UNIC_USER_BPE				BIT(2) /* broadcast promisc enabled by user */
#define UNIC_OVERFLOW_MGP			BIT(3) /* mulitcast guid overflow */
#define UNIC_OVERFLOW_IPP			BIT(4) /* unicast ip overflow */
#define UNIC_UPE				(UNIC_USER_UPE | \
						 UNIC_OVERFLOW_IPP)
#define UNIC_MPE				(UNIC_USER_MPE | \
						 UNIC_OVERFLOW_MGP)

#define UNIC_RSS_MAX_VL_NUM		UBASE_NIC_MAX_VL_NUM
#define UNIC_INVALID_PRIORITY		(0xff)
#define UNIC_MAX_PRIO_NUM		IEEE_8021QAZ_MAX_TCS
#define UNIC_VL_TSA_DWRR		IEEE_8021QAZ_TSA_ETS

/* must be consistent with definition in firmware */
enum unic_media_type {
	UNIC_MEDIA_TYPE_UNKNOWN,
	UNIC_MEDIA_TYPE_FIBER,
	UNIC_MEDIA_TYPE_BACKPLANE,
	UNIC_MEDIA_TYPE_NONE,
};

/* must be consistent with definition in firmware */
enum unic_module_type {
	UNIC_MODULE_TYPE_UNKNOWN	= 0x00,
	UNIC_MODULE_TYPE_FIBRE_LR	= 0x01,
	UNIC_MODULE_TYPE_FIBRE_SR	= 0x02,
	UNIC_MODULE_TYPE_AOC		= 0x03,
	UNIC_MODULE_TYPE_CR		= 0x04,
	UNIC_MODULE_TYPE_KR		= 0x05,
	UNIC_MODULE_TYPE_TP		= 0x06,
};

#define UNIC_LANES_1	1
#define UNIC_LANES_2	2
#define UNIC_LANES_4	4
#define UNIC_LANES_8	8

#define UNIC_SUPPORT_200G_X2_BIT	BIT(0)
#define UNIC_SUPPORT_200G_X4_BIT	BIT(1)
#define UNIC_SUPPORT_400G_X4_BIT	BIT(2)
#define UNIC_SUPPORT_400G_X8_BIT	BIT(3)
#define UNIC_SUPPORT_25G_X1_BIT		BIT(4)
#define UNIC_SUPPORT_50G_X1_BIT		BIT(5)
#define UNIC_SUPPORT_50G_X2_BIT		BIT(6)
#define UNIC_SUPPORT_100G_X1_BIT	BIT(7)
#define UNIC_SUPPORT_100G_X2_BIT	BIT(8)
#define UNIC_SUPPORT_100G_X4_BIT	BIT(9)
#define UNIC_SUPPORT_10G_X1_BIT		BIT(10)

enum unic_mac_speed {
	UNIC_MAC_SPEED_UNKNOWN	= 0,
	UNIC_MAC_SPEED_10G	= SPEED_10000, /* 10000 Mbps = 10 Gbps */
	UNIC_MAC_SPEED_25G	= SPEED_25000, /* 25000 Mbps = 25 Gbps */
	UNIC_MAC_SPEED_50G	= SPEED_50000, /* 50000 Mbps = 50 Gbps */
	UNIC_MAC_SPEED_100G	= SPEED_100000, /* 100000 Mbps = 100 Gbps */
	UNIC_MAC_SPEED_200G	= SPEED_200000, /* 200000 Mbps = 200 Gbps */
	UNIC_MAC_SPEED_400G	= SPEED_400000, /* 400000 Mbps = 400 Gbps */
};

#define UNIC_MBYTE_PER_SEND	125000

#endif

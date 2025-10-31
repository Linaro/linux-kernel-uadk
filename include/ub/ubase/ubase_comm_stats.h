/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved.
 *
 */

#ifndef _UB_UBASE_COMM_STATS_H_
#define _UB_UBASE_COMM_STATS_H_

#include <linux/auxiliary_bus.h>

struct ubase_eth_mac_stats {
	u64	tx_fragment_pkts;
	u64	tx_undersize_pkts;
	u64	tx_undermin_pkts;

	u64	tx_64_octets_pkts;
	u64	tx_65_127_octets_pkts;
	u64	tx_128_255_octets_pkts;
	u64	tx_256_511_octets_pkts;
	u64	tx_512_1023_octets_pkts;
	u64	tx_1024_1518_octets_pkts;
	u64	tx_1519_2047_octets_pkts;
	u64	tx_2048_4095_octets_pkts;
	u64	tx_4096_8191_octets_pkts;
	u64	tx_8192_9216_octets_pkts;
	u64	tx_9217_12287_octets_pkts;
	u64	tx_12288_16383_octets_pkts;
	u64	tx_1519_max_octets_bad_pkts;
	u64	tx_1519_max_octets_good_pkts;
	u64	tx_oversize_pkts;
	u64	tx_jabber_pkts;
	u64	tx_bad_pkts;
	u64	tx_bad_octets;
	u64	tx_good_pkts;
	u64	tx_good_octets;
	u64	tx_total_pkts;
	u64	tx_total_octets;
	u64	tx_unicast_pkts;
	u64	tx_multicast_pkts;
	u64	tx_broadcast_pkts;

	u64	tx_pause_pkts;
	u64	tx_pfc_pkts;
	u64	tx_pri0_pfc_pkts;
	u64	tx_pri1_pfc_pkts;
	u64	tx_pri2_pfc_pkts;
	u64	tx_pri3_pfc_pkts;
	u64	tx_pri4_pfc_pkts;
	u64	tx_pri5_pfc_pkts;
	u64	tx_pri6_pfc_pkts;
	u64	tx_pri7_pfc_pkts;

	u64	tx_mac_ctrl_pkts;
	u64	tx_unfilter_pkts;
	u64	tx_1588_pkts;
	u64	tx_err_all_pkts;
	u64	tx_from_app_good_pkts;
	u64	tx_from_app_bad_pkts;

	u64	rx_fragment_pkts;
	u64	rx_undersize_pkts;
	u64	rx_undermin_pkts;

	u64	rx_64_octets_pkts;
	u64	rx_65_127_octets_pkts;
	u64	rx_128_255_octets_pkts;
	u64	rx_256_511_octets_pkts;
	u64	rx_512_1023_octets_pkts;
	u64	rx_1024_1518_octets_pkts;
	u64	rx_1519_2047_octets_pkts;
	u64	rx_2048_4095_octets_pkts;
	u64	rx_4096_8191_octets_pkts;
	u64	rx_8192_9216_octets_pkts;
	u64	rx_9217_12287_octets_pkts;
	u64	rx_12288_16383_octets_pkts;
	u64	rx_1519_max_octets_bad_pkts;
	u64	rx_1519_max_octets_good_pkts;

	u64	rx_oversize_pkts;
	u64	rx_jabber_pkts;
	u64	rx_bad_pkts;
	u64	rx_bad_octets;
	u64	rx_good_pkts;
	u64	rx_good_octets;
	u64	rx_total_pkts;
	u64	rx_total_octets;
	u64	rx_unicast_pkts;
	u64	rx_multicast_pkts;
	u64	rx_broadcast_pkts;

	u64	rx_pause_pkts;
	u64	rx_pfc_pkts;
	u64	rx_pri0_pfc_pkts;
	u64	rx_pri1_pfc_pkts;
	u64	rx_pri2_pfc_pkts;
	u64	rx_pri3_pfc_pkts;
	u64	rx_pri4_pfc_pkts;
	u64	rx_pri5_pfc_pkts;
	u64	rx_pri6_pfc_pkts;
	u64	rx_pri7_pfc_pkts;

	u64	rx_mac_ctrl_pkts;
	u64	rx_symbol_err_pkts;
	u64	rx_fcs_err_pkts;
	u64	rx_send_app_good_pkts;
	u64	rx_send_app_bad_pkts;
	u64	rx_unfilter_pkts;

	u64	tx_merge_frame_ass_error_pkts;
	u64	tx_merge_frame_ass_ok_pkts;
	u64	tx_merge_frame_frag_count;
	u64	rx_merge_frame_ass_error_pkts;
	u64	rx_merge_frame_ass_ok_pkts;
	u64	rx_merge_frame_frag_count;
	u64	rx_merge_frame_smd_error_pkts;
};

#endif /* _UBASE_COMM_STATS_H */

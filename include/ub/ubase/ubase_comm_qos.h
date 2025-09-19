/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved.
 *
 */

#ifndef _UB_UBASE_COMM_QOS_H_
#define _UB_UBASE_COMM_QOS_H_

#include <linux/dcbnl.h>
#include <ub/ubus/ubus.h>
#include <ub/ubase/ubase_comm_dev.h>

enum ubase_sl_sched_mode {
	UBASE_SL_SP = IEEE_8021QAZ_TSA_STRICT,
	UBASE_SL_DWRR = IEEE_8021QAZ_TSA_ETS,
};

struct ubase_sl_priqos {
	u32 port_bitmap;
	u32 sl_bitmap;
	u8 weight[UBASE_MAX_SL_NUM];
	u8 sch_mode[UBASE_MAX_SL_NUM];
};

int ubase_set_priqos_info(struct device *dev, struct ubase_sl_priqos *sl_priqos);
int ubase_get_priqos_info(struct device *dev, struct ubase_sl_priqos *sl_priqos);
int ubase_check_qos_sch_param(struct auxiliary_device *adev, u16 vl_bitmap,
			      u8 *vl_bw, u8 *vl_tsa, bool is_ets);
int ubase_config_tm_vl_sch(struct auxiliary_device *adev, u16 vl_bitmap,
			   u8 *vl_bw, u8 *vl_tsa);
void ubase_update_udma_dscp_vl(struct auxiliary_device *adev, u8 *dscp_vl,
			       u8 dscp_num);

#endif

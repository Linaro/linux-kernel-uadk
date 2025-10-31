/* SPDX-License-Identifier: GPL-2.0+ */
/* Copyright(c) 2025 HiSilicon Technologies CO., Ltd. All rights reserved. */

#ifndef __UDMA_RCT_H__
#define __UDMA_RCT_H__

#include "udma_common.h"

#define RC_TYPE 2U
#define RC_READY_STATE 1U
#define RC_AVAIL_SGMT_OST 512U

#define RCE_TOKEN_ID_L_MASK GENMASK(11, 0)
#define RCE_TOKEN_ID_H_OFFSET 12U
#define RCE_ADDR_L_OFFSET 12U
#define RCE_ADDR_L_MASK GENMASK(19, 0)
#define RCE_ADDR_H_OFFSET 32U

struct udma_rc_queue {
	uint32_t id;
	struct udma_buf buf;
};

struct udma_rc_ctx {
	/* DW0 */
	uint32_t rsv0 : 5;
	uint32_t type : 3;
	uint32_t rce_shift : 4;
	uint32_t rsv1 : 4;
	uint32_t state : 3;
	uint32_t rsv2 : 1;
	uint32_t rce_token_id_l : 12;
	/* DW1 */
	uint32_t rce_token_id_h : 8;
	uint32_t rsv3 : 4;
	uint32_t rce_base_addr_l : 20;
	/* DW2 */
	uint32_t rce_base_addr_h;
	/* DW3~DW31 */
	uint32_t rsv4[28];
	uint32_t avail_sgmt_ost : 10;
	uint32_t rsv5 : 22;
	/* DW32~DW63 */
	uint32_t rsv6[32];
};

struct udma_vir_cap {
	uint8_t ue_idx;
	uint8_t virtualization : 1;
	uint8_t rsv : 7;
};

int udma_config_device(struct ubcore_device *ubcore_dev,
		       struct ubcore_device_cfg *cfg);
void udma_free_rc_queue(struct udma_dev *dev, int rc_queue_id);

#endif /* __UDMA_RCT_H__ */

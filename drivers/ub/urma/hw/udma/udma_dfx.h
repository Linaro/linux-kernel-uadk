/* SPDX-License-Identifier: GPL-2.0+ */
/* Copyright(c) 2025 HiSilicon Technologies CO., Ltd. All rights reserved. */

#ifndef __UDMA_DFX_H__
#define __UDMA_DFX_H__

#include "udma_dev.h"
#include "udma_ctx.h"
#include "udma_jetty.h"

#define UDMA_RX_THRESHOLD_0 0
#define UDMA_RX_THRESHOLD_64 64
#define UDMA_RX_THRESHOLD_512 512
#define UDMA_RX_THRESHOLD_4096 4096

#define UDMA_RX_REQ_EPSN_H_SHIFT 16

enum udma_limit_wl {
	UDMA_LIMIT_WL_0,
	UDMA_LIMIT_WL_64,
	UDMA_LIMIT_WL_512,
	UDMA_LIMIT_WL_4096,
};

struct udma_dfx_entity_initialization {
	rwlock_t	*rwlock;
	struct xarray	*table;
};

struct udma_dfx_entity_cnt {
	rwlock_t		*rwlock;
	struct udma_dfx_entity	*entity;
	uint32_t		*res_cnt;
};

static inline uint32_t to_udma_rx_threshold(uint32_t limit_wl)
{
	switch (limit_wl) {
	case UDMA_LIMIT_WL_0:
		return UDMA_RX_THRESHOLD_0;
	case UDMA_LIMIT_WL_64:
		return UDMA_RX_THRESHOLD_64;
	case UDMA_LIMIT_WL_512:
		return UDMA_RX_THRESHOLD_512;
	default:
		return UDMA_RX_THRESHOLD_4096;
	}
}

int udma_query_res(struct ubcore_device *dev, struct ubcore_res_key *key,
		   struct ubcore_res_val *val);
int udma_dfx_init(struct udma_dev *udma_dev);
void udma_dfx_uninit(struct udma_dev *udma_dev);

#endif /* __UDMA_DFX_H__ */

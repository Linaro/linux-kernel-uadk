/* SPDX-License-Identifier: GPL-2.0+ */
/* Copyright(c) 2025 HiSilicon Technologies CO., Ltd. All rights reserved. */

#ifndef __UDMA_JFC_H__
#define __UDMA_JFC_H__

#include "udma_dev.h"
#include "udma_ctx.h"

struct udma_jfc {
	struct ubcore_jfc base;
	uint32_t jfcn;
	uint32_t ceqn;
	uint32_t tid;
	struct udma_buf buf;
	struct udma_sw_db db;
	uint32_t ci;
	uint32_t arm_sn; /* only kernel mode use */
	spinlock_t lock;
	refcount_t event_refcount;
	struct completion event_comp;
	uint32_t lock_free;
	uint32_t inline_en;
	uint32_t mode;
	uint64_t stars_chnl_addr;
	bool stars_en;
	uint32_t cq_shift;
};

static inline struct udma_jfc *to_udma_jfc(struct ubcore_jfc *jfc)
{
	return container_of(jfc, struct udma_jfc, base);
}

#endif /* __UDMA_JFC_H__ */

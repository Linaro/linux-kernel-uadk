/* SPDX-License-Identifier: GPL-2.0+ */
/* Copyright(c) 2025 HiSilicon Technologies CO., Ltd. All rights reserved. */

#ifndef __UDMA_JFR_H__
#define __UDMA_JFR_H__

#include "udma_dev.h"
#include "udma_ctx.h"
#include "udma_common.h"

struct udma_jfr_idx_que {
	struct udma_buf buf;
	struct udma_table jfr_idx_table;
};

struct udma_jfr {
	struct ubcore_jfr ubcore_jfr;
	struct udma_jetty_queue rq;
	struct udma_jfr_idx_que idx_que;
	struct udma_sw_db sw_db;
	struct udma_sw_db jfr_sleep_buf;
	struct udma_context *udma_ctx;
	uint32_t rx_threshold;
	uint32_t wqe_cnt;
	uint64_t jetty_addr;
	enum ubcore_jfr_state state;
	uint32_t max_sge;
	spinlock_t lock;
	refcount_t ae_refcount;
	struct completion ae_comp;
};

static inline struct udma_jfr *to_udma_jfr(struct ubcore_jfr *jfr)
{
	return container_of(jfr, struct udma_jfr, ubcore_jfr);
}

static inline struct udma_jfr *to_udma_jfr_from_queue(struct udma_jetty_queue *queue)
{
	return container_of(queue, struct udma_jfr, rq);
}

#endif /* __UDMA_JFR_H__ */

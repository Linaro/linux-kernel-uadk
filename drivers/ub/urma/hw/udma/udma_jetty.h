/* SPDX-License-Identifier: GPL-2.0+ */
/* Copyright(c) 2025 HiSilicon Technologies CO., Ltd. All rights reserved. */

#ifndef __UDMA_JETTY_H__
#define __UDMA_JETTY_H__

#include "udma_common.h"

struct udma_jetty {
	struct ubcore_jetty ubcore_jetty;
	struct udma_jfr *jfr;
	struct udma_jetty_queue sq;
	uint64_t jetty_addr;
	refcount_t ae_refcount;
	struct completion ae_comp;
	bool pi_type;
	bool ue_rx_closed;
};

static inline struct udma_jetty *to_udma_jetty(struct ubcore_jetty *jetty)
{
	return container_of(jetty, struct udma_jetty, ubcore_jetty);
}

static inline struct udma_jetty *to_udma_jetty_from_queue(struct udma_jetty_queue *queue)
{
	return container_of(queue, struct udma_jetty, sq);
}

#endif /* __UDMA_JETTY_H__ */

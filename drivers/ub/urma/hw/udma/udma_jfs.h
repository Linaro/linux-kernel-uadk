/* SPDX-License-Identifier: GPL-2.0+ */
/* Copyright(c) 2025 HiSilicon Technologies CO., Ltd. All rights reserved. */

#ifndef __UDMA_JFS_H__
#define __UDMA_JFS_H__

#include "udma_common.h"

struct udma_jfs {
	struct ubcore_jfs ubcore_jfs;
	struct udma_jetty_queue sq;
	uint64_t jfs_addr;
	refcount_t ae_refcount;
	struct completion ae_comp;
	uint32_t mode;
	bool pi_type;
	bool ue_rx_closed;
};

static inline struct udma_jfs *to_udma_jfs(struct ubcore_jfs *jfs)
{
	return container_of(jfs, struct udma_jfs, ubcore_jfs);
}

static inline struct udma_jfs *to_udma_jfs_from_queue(struct udma_jetty_queue *queue)
{
	return container_of(queue, struct udma_jfs, sq);
}

#endif /* __UDMA_JFS_H__ */

/* SPDX-License-Identifier: GPL-2.0+ */
/* Copyright(c) 2025 HiSilicon Technologies CO., Ltd. All rights reserved. */

#ifndef __UDMA_COMM_H__
#define __UDMA_COMM_H__

#include <linux/jhash.h>
#include <ub/urma/ubcore_api.h>
#include "udma_dev.h"

struct udma_jetty_grp {
	struct ubcore_jetty_group ubcore_jetty_grp;
	uint32_t start_jetty_id;
	uint32_t next_jetty_id;
	uint32_t jetty_grp_id;
	uint32_t valid;
	struct mutex valid_lock;
	refcount_t ae_refcount;
	struct completion ae_comp;
};

struct udma_jetty_queue {
	struct udma_buf buf;
	void *kva_curr;
	uint32_t id;
	void __iomem *db_addr;
	void __iomem *dwqe_addr;
	uint32_t pi;
	uint32_t ci;
	uintptr_t *wrid;
	spinlock_t lock;
	uint32_t max_inline_size;
	uint32_t max_sge_num;
	uint32_t tid;
	bool flush_flag;
	uint32_t old_entry_idx;
	enum ubcore_transport_mode trans_mode;
	struct ubcore_tjetty *rc_tjetty;
	bool is_jetty;
	uint32_t sqe_bb_cnt;
	uint32_t lock_free; /* Support kernel mode lock-free mode */
	uint32_t ta_timeout; /* ms */
	enum ubcore_jetty_state state;
	bool non_pin;
	struct udma_jetty_grp *jetty_grp;
	enum udma_jetty_type jetty_type;
};

int pin_queue_addr(struct udma_dev *dev, uint64_t addr,
		   uint32_t len, struct udma_buf *buf);
void unpin_queue_addr(struct ubcore_umem *umem);

struct udma_umem_param {
	struct ubcore_device *ub_dev;
	uint64_t va;
	uint64_t len;
	union ubcore_umem_flag flag;
	bool is_kernel;
};

struct ubcore_umem *udma_umem_get(struct udma_umem_param *param);
void udma_umem_release(struct ubcore_umem *umem, bool is_kernel);
void udma_init_udma_table(struct udma_table *table, uint32_t max, uint32_t min);
void udma_init_udma_table_mutex(struct xarray *table, struct mutex *udma_mutex);
void udma_destroy_udma_table(struct udma_dev *dev, struct udma_table *table,
			     const char *table_name);
void udma_destroy_eid_table(struct udma_dev *udma_dev);
int udma_k_alloc_buf(struct udma_dev *udma_dev, size_t memory_size, struct udma_buf *buf);
void udma_k_free_buf(struct udma_dev *udma_dev, size_t memory_size, struct udma_buf *buf);
void *udma_alloc_iova(struct udma_dev *udma_dev, size_t memory_size, dma_addr_t *addr);
void udma_free_iova(struct udma_dev *udma_dev, size_t memory_size, void *kva_or_slot,
		    dma_addr_t addr);
static inline uint64_t udma_cal_npages(uint64_t va, uint64_t len)
{
	return (ALIGN(va + len, PAGE_SIZE) - ALIGN_DOWN(va, PAGE_SIZE)) / PAGE_SIZE;
}

#endif /* __UDMA_COMM_H__ */

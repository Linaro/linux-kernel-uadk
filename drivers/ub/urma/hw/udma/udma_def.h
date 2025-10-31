/* SPDX-License-Identifier: GPL-2.0+ */
/* Copyright(c) 2025 HiSilicon Technologies CO., Ltd. All rights reserved. */

#ifndef __UDMA_DEF_H__
#define __UDMA_DEF_H__

#include <linux/types.h>
#include <linux/auxiliary_bus.h>
#include <linux/ummu_core.h>
#include <ub/ubase/ubase_comm_mbx.h>

enum {
	UDMA_CAP_FEATURE_AR		= BIT(0),
	UDMA_CAP_FEATURE_JFC_INLINE	= BIT(4),
	UDMA_CAP_FEATURE_DIRECT_WQE	= BIT(11),
	UDMA_CAP_FEATURE_CONG_CTRL	= BIT(16),
	UDMA_CAP_FEATURE_REDUCE		= BIT(17),
	UDMA_CAP_FEATURE_UE_RX_CLOSE	= BIT(18),
	UDMA_CAP_FEATURE_RNR_RETRY	= BIT(19),
};

struct udma_res {
	uint32_t max_cnt;
	uint32_t start_idx;
	uint32_t next_idx;
	uint32_t depth;
};

struct udma_tbl {
	uint32_t max_cnt;
	uint32_t size;
};

struct udma_caps {
	unsigned long init_flag;
	struct udma_res jfs;
	struct udma_res jfr;
	struct udma_res jfc;
	struct udma_res jetty;
	struct udma_res jetty_grp;
	uint32_t jetty_in_grp;
	uint32_t jfs_sge;
	uint32_t jfr_sge;
	uint32_t jfs_rsge;
	uint32_t jfs_inline_sz;
	uint32_t comp_vector_cnt;
	uint16_t ue_cnt;
	uint8_t ue_id;
	uint32_t trans_mode;
	uint32_t max_msg_len;
	uint32_t feature;
	uint32_t rsvd_jetty_cnt;
	uint32_t max_read_size;
	uint32_t max_write_size;
	uint32_t max_cas_size;
	uint32_t max_fetch_and_add_size;
	uint32_t atomic_feat;
	struct udma_res ccu_jetty;
	struct udma_res hdc_jetty;
	struct udma_res stars_jetty;
	struct udma_res public_jetty;
	struct udma_res user_ctrl_normal_jetty;
	uint16_t rc_queue_num;
	uint16_t rc_queue_depth;
	uint8_t rc_entry_size;
	uint8_t ack_queue_num;
	uint8_t port_num;
	uint8_t cqe_size;
	struct udma_tbl seid;
};

struct udma_buf {
	dma_addr_t		addr;
	union {
		void			*kva; /* used for kernel mode */
		struct iova_slot	*slot;
		void			*kva_or_slot;
	};
	void			*aligned_va;
	struct ubcore_umem	*umem;
	uint32_t		entry_size;
	uint32_t		entry_cnt;
	uint32_t		cnt_per_page_shift;
	struct xarray		id_table_xa;
	struct mutex		id_table_mutex;
};

enum num_elem_in_grp {
	NUM_TP_PER_GROUP = 16,
	NUM_JETTY_PER_GROUP = 32,
};

enum {
	RCT_INIT_FLAG,
};

#endif /* __UDMA_DEF_H__ */

/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) HiSilicon Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef __DECODER_H__
#define __DECODER_H__

#include <ub/ubus/ubus.h>

struct reg_default_val {
	u32 cmdq_cfg_val;
	u32 evtq_cfg_val;
};

struct page_table_desc {
	void *page_base;
	dma_addr_t page_dma;
	u32 count;
};

struct page_table {
	void *pgtlb_base;
	dma_addr_t pgtlb_dma;
	struct page_table_desc *desc_base;
};

struct ub_decoder {
	struct device *dev;
	struct ub_entity *uent;
	phys_addr_t mmio_base_addr;
	u32 mmio_size_sup;
	u64 rg_size;
	struct page_table pgtlb;
	struct reg_default_val vals;
	int irq_num;

	struct page_table_desc invalid_desc;
	u64 invalid_page_dma;

	struct mutex table_lock;
};

void ub_decoder_init(struct ub_entity *uent);
void ub_decoder_uninit(struct ub_entity *uent);

#endif /* __DECODER_H__ */

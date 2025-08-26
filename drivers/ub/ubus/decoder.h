/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) HiSilicon Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef __DECODER_H__
#define __DECODER_H__

#include <ub/ubus/ubus.h>

enum ub_cmd_op_type {
	INVALID = 0x0,
	SYNC = 0x1,
	TLBI_ALL = 0x2,
	TLBI_PARTIAL = 0x3,
};

struct queue_idx {
	union {
		u32 val;
		struct {
			u32 cmdq_wr_idx : 11;
			u32 reserved1 : 5;
			u32 cmdq_err_resp : 1;
			u32 reserved2 : 15;
		};
		struct {
			u32 cmdq_rd_idx : 11;
			u32 reserved3 : 5;
			u32 cmdq_err : 1;
			u32 cmdq_err_reason : 3;
			u32 reserved4 : 12;
		};
		struct {
			u32 eventq_wr_idx : 11;
			u32 reserved5 : 21;
		};
		struct {
			u32 eventq_rd_idx : 11;
			u32 reserved6 : 20;
			u32 eventq_ovfl_err_resp : 1;
		};
	};
};

struct ub_decoder_queue {
	phys_addr_t base;
	void __iomem *qbase;
	u32 qs;
	struct queue_idx prod;
	struct queue_idx cons;
};

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
	struct ub_decoder_queue cmdq;
	struct ub_decoder_queue evtq;
	struct page_table pgtlb;
	struct reg_default_val vals;
	void __iomem *notify;
	int irq_num;

	struct page_table_desc invalid_desc;
	u64 invalid_page_dma;

	struct mutex table_lock;
};

#define DECODER_PGTBL_PGPRT_MASK GENMASK_ULL(47, 12)
#define DECODER_DMA_PAGE_ADDR_OFFSET 12

#define PGTLB_CACHE_IR_NC	0b00
#define PGTLB_CACHE_IR_WBRA	0b01
#define PGTLB_CACHE_IR_WT	0b10
#define PGTLB_CACHE_IR_WB	0b11
#define PGTLB_CACHE_OR_NC	0b0000
#define PGTLB_CACHE_OR_WBRA	0b0100
#define PGTLB_CACHE_OR_WT	0b1000
#define PGTLB_CACHE_OR_WB	0b1100
#define PGTLB_CACHE_SH_NSH	0b000000
#define PGTLB_CACHE_SH_OSH	0b100000
#define PGTLB_CACHE_SH_ISH	0b110000

#define PGTLB_ATTR_DEFAULT	(PGTLB_CACHE_IR_WBRA |	\
				 PGTLB_CACHE_OR_WBRA |	\
				 PGTLB_CACHE_SH_ISH)

#define RGTLB_TO_PGTLB 8
#define DECODER_PAGE_ENTRY_SIZE 64
#define DECODER_PAGE_SIZE (1 << 12)
#define DECODER_PAGE_TABLE_SIZE (1 << 12)
#define PAGE_TABLE_PAGE_SIZE (DECODER_PAGE_ENTRY_SIZE * DECODER_PAGE_SIZE)
#define RANGE_TABLE_PAGE_SIZE (DECODER_PAGE_ENTRY_SIZE * \
			       DECODER_PAGE_SIZE * \
			       RGTLB_TO_PGTLB)

void ub_decoder_init(struct ub_entity *uent);
void ub_decoder_uninit(struct ub_entity *uent);
int ub_decoder_cmd_request(struct ub_decoder *decoder, phys_addr_t addr,
			   u64 size, enum ub_cmd_op_type op);
#endif /* __DECODER_H__ */

// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) HiSilicon Technologies Co., Ltd. 2025. All rights reserved.
 */

#define pr_fmt(fmt)	"ubus omm: " fmt

#include <linux/dma-mapping.h>
#include "decoder.h"
#include "omm.h"

enum entry_type {
	INVALID_ENTRY = 0x0,
	PAGE_TABLE = 0x1,
	PAGE = 0x2,
	RANGE_TABLE = 0x3,
};

struct page_table_entry {
	u64 entry_type : 2;
	u64 reserve1 : 10;
	u64 next_lv_addr : 36;
	u64 reserve2 : 8;
	u64 pgtlb_attr : 8;
};

struct page_entry {
	/* dw0 ~ dw1 */
	u64 entry_type : 2;
	u64 reserve0 : 6;
	u64 token_vld : 1;
	u64 reserve1 : 3;
	u64 ub_addr : 52;
	/* dw2 ~ dw3 */
	u64 reserve2 : 32;
	u64 tpg_num : 20;
	u64 order_type : 2;
	u64 order_id : 8;
	u64 reserve3 : 2;
	/* dw4 ~ dw5 */
	u64 dst_eid_low;
	/* dw6 ~ dw7 */
	u64 dst_eid_high;
	/* dw8 ~ dw9 */
	u64 token_id : 20;
	u64 reserve4 : 12;
	u64 token_value : 32;
	/* dw10 ~ dw11 */
	u64 upi : 16;
	u64 reserve5 : 48;
	/* dw12 ~ dw13 */
	u64 reserve6;
	/* dw14 ~ dw15 */
	u64 reserve7 : 32;
	u64 src_eid : 20;
	u64 reserve8 : 12;
};

struct range_table_entry {
	/* dw0 ~ dw1 */
	u64 entry_type : 2;
	u64 reserve0 : 6;
	u64 token_vld : 1;
	u64 reserve1 : 3;
	u64 next_lv_addr : 36;
	u64 reserve2 : 8;
	u64 page_table_attr : 8;
	/* dw2 ~ dw3 */
	u64 mem_base : 15;
	u64 reserve3 : 1;
	u64 mem_limit : 15;
	u64 reserve4 : 1;
	u64 reserve5 : 32;
	/* dw4 ~ dw5 */
	u64 reserve6 : 12;
	u64 ub_addr : 52;
	/* dw6 ~ dw7 */
	u64 reserve7 : 32;
	u64 tpg_num : 20;
	u64 order_type : 2;
	u64 order_id : 8;
	u64 reserve8 : 2;
	/* dw8 ~ dw9 */
	u64 dst_eid_low;
	/* dw10 ~ dw11 */
	u64 dst_eid_high;
	/* dw12 ~ dw13 */
	u64 token_id : 20;
	u64 reserve9 : 12;
	u64 token_value : 32;
	/* dw14 ~ dw15 */
	u64 upi : 16;
	u64 reserve10 : 16;
	u64 src_eid : 20;
	u64 reserve11 : 12;
};

#define UBA_ADDR_OFFSET			12

#define DECODER_PAGE_INDEX_LOC		20
#define DECODER_PAGE_TABLE_LOC		35
#define DECODER_SUB_PAGE_TABLE_LOC	32
#define DECODER_PAGE_TABLE_MASK		GENMASK_ULL(43, 35)
#define DECODER_SUB_PAGE_TABLE_MASK	GENMASK_ULL(34, 32)
#define DECODER_PGTLB_PAGE_MASK		GENMASK_ULL(31, 20)
#define DECODER_RGTLB_PAGE_MASK		GENMASK_ULL(34, 20)
#define DECODER_RGTLB_ADDRESS_MASK	GENMASK_ULL(34, 20)
#define DECODER_RGTLB_ADDRESS_OFFSET	20
#define TOKEN_VALID_MASK		GENMASK(0, 0)

#define get_pgtlb_idx(decoder, pa) ((((pa) - (decoder)->mmio_base_addr) & \
				    DECODER_PAGE_TABLE_MASK) >> \
				    DECODER_PAGE_TABLE_LOC)

#define get_rgtlb_addr(decoder, pa) ((((pa) - (decoder)->mmio_base_addr) & \
				     DECODER_RGTLB_ADDRESS_MASK) >> \
				     DECODER_RGTLB_ADDRESS_OFFSET)

#define get_page_idx(decoder, pa) ((((pa) - (decoder)->mmio_base_addr) & \
				   DECODER_PGTLB_PAGE_MASK) >> \
				   DECODER_PAGE_INDEX_LOC)

#define get_pgtlb_sub_idx(decoder, pa) ((((pa) - (decoder)->mmio_base_addr) & \
					DECODER_SUB_PAGE_TABLE_MASK) >> \
					DECODER_SUB_PAGE_TABLE_LOC)

static void fill_page_entry(struct page_entry *page,
			    struct decoder_map_info *info, u64 offset)
{
	page->entry_type = PAGE;
	page->token_vld = ubc_feature & TOKEN_VALID_MASK;
	page->ub_addr = (info->uba + offset) >> UBA_ADDR_OFFSET;
	page->tpg_num = info->tpg_num;
	page->order_type = info->order_type;
	page->order_id = info->order_id;
	page->dst_eid_low = info->eid_low;
	page->dst_eid_high = info->eid_high;
	page->token_id = info->token_id;
	page->token_value = info->token_value;
	page->upi = info->upi;
	page->src_eid = info->src_eid;
}

static int handle_range_table(struct ub_decoder *decoder, u64 *offset,
			      struct decoder_map_info *info, bool is_map)
{
	return 0;
}

static int pgtlb_alloc_page(struct ub_decoder *decoder,
			    struct page_table_desc *desc,
			    struct page_table_entry *pgtlb_entry)
{
	void *page_base;

	page_base = dmam_alloc_coherent(decoder->dev, PAGE_TABLE_PAGE_SIZE,
					&desc->page_dma, GFP_KERNEL);
	if (!page_base)
		return -ENOMEM;

	desc->page_base = page_base;
	desc->count = 0;
	pgtlb_entry->entry_type = PAGE_TABLE;
	pgtlb_entry->next_lv_addr = (desc->page_dma &
				    DECODER_PGTBL_PGPRT_MASK) >>
				    DECODER_DMA_PAGE_ADDR_OFFSET;
	pgtlb_entry->pgtlb_attr = PGTLB_ATTR_DEFAULT;
	return 0;
}

static void pgtlb_free_page(struct ub_decoder *decoder,
			    struct page_table_desc *desc,
			    struct page_table_entry *pgtlb_entry)
{
	dmam_free_coherent(decoder->dev, PAGE_TABLE_PAGE_SIZE, desc->page_base,
			   desc->page_dma);
	pgtlb_entry->next_lv_addr = decoder->invalid_page_dma;
	pgtlb_entry->entry_type = PAGE_TABLE;
	desc->page_base = NULL;
	desc->page_dma = 0;
}

static int pgtlb_map_to_page(struct ub_decoder *decoder,
			     struct page_table_desc *desc,
			     struct page_table_entry *pgtlb_entry,
			     struct decoder_map_info *info, u64 offset)
{
	struct page_entry *page;
	u32 index;
	int ret;

	if (!desc->page_base) {
		ret = pgtlb_alloc_page(decoder, desc, pgtlb_entry);
		if (ret)
			return ret;
	}

	index = get_page_idx(decoder, info->pa + offset);

	page = (struct page_entry *)desc->page_base + index;

	/*
	 * If it's the first time to apply for a page table,
	 * the error branch will not be taken.
	 */
	if (page->entry_type != INVALID_ENTRY) {
		ub_err(decoder->uent, "decoder mapping page from page table error.\n");
		return -EINVAL;
	}

	fill_page_entry(page, info, offset);
	desc->count++;
	return 0;
}

static int pgtlb_unmap_to_page(struct ub_decoder *decoder,
			       struct page_table_desc *desc,
			       struct page_table_entry *pgtlb_entry,
			       struct decoder_map_info *info, u64 offset)
{
	struct page_entry *page;
	u32 index;

	index = get_page_idx(decoder, info->pa + offset);
	page = (struct page_entry *)desc->page_base + index;

	if (pgtlb_entry->next_lv_addr == decoder->invalid_page_dma ||
	    page->entry_type != PAGE) {
		ub_err(decoder->uent, "decoder unmap page from page table error.\n");
		return -EINVAL;
	}

	page->entry_type = INVALID_ENTRY;
	desc->count--;

	if (desc->count == 0)
		pgtlb_free_page(decoder, desc, pgtlb_entry);
	return 0;
}

static int handle_page_table(struct ub_decoder *decoder, u64 *offset,
			     struct decoder_map_info *info, bool is_map)
{
	struct page_table_entry *pgtlb_entry;
	struct page_table_desc *desc;
	struct page_table *pgtlb;
	u32 table_idx;
	u32 sub_index;
	int ret;

	pgtlb = &decoder->pgtlb;
	table_idx = get_pgtlb_idx(decoder, info->pa + *offset);
	sub_index = get_pgtlb_sub_idx(decoder, info->pa + *offset);
	desc = pgtlb->desc_base + table_idx * RGTLB_TO_PGTLB + sub_index;
	pgtlb_entry = (struct page_table_entry *)pgtlb->pgtlb_base +
		      table_idx * RGTLB_TO_PGTLB + sub_index;

	if (is_map)
		ret = pgtlb_map_to_page(decoder, desc, pgtlb_entry, info,
					*offset);
	else
		ret = pgtlb_unmap_to_page(decoder, desc, pgtlb_entry, info,
					  *offset);
	*offset += SZ_1M;
	return ret;
}

#define MEM_LMT_MAX		0x7FFF
#define RANGE_UBA_LOW_MASK	GENMASK_ULL(34, 20)
#define RANGE_UBA_HIGH_MASK	GENMASK_ULL(63, 35)
#define UBA_CARRY		0x800000000
#define UBA_NOCARRY		0x0

static void fill_range_table(struct ub_decoder *decoder,
			     struct range_table_entry *rg_entry,
			     struct decoder_map_info *info, u64 *offset)
{
	u64 mem_base, mem_limit;
	u64 uba_high, uba_low;
	u64 size;

	mem_base = get_rgtlb_addr(decoder, info->pa + *offset);
	size = (MEM_LMT_MAX - mem_base + 1ULL) << DECODER_RGTLB_ADDRESS_OFFSET;

	uba_low = (info->uba + *offset) & RANGE_UBA_LOW_MASK;
	uba_high = (info->uba + *offset) & RANGE_UBA_HIGH_MASK;

	if (info->size - *offset >= size) {
		mem_limit = MEM_LMT_MAX;
		*offset += size;
	} else {
		mem_limit = get_rgtlb_addr(decoder, info->pa + info->size);
		mem_limit--;
		*offset += (mem_limit - mem_base + 1) <<
			   DECODER_RGTLB_ADDRESS_OFFSET;
	}

	rg_entry->entry_type = RANGE_TABLE;
	rg_entry->next_lv_addr = decoder->invalid_page_dma;
	rg_entry->page_table_attr = PGTLB_ATTR_DEFAULT;
	rg_entry->token_vld = ubc_feature & TOKEN_VALID_MASK;
	rg_entry->mem_base = mem_base;
	rg_entry->mem_limit = mem_limit;
	rg_entry->ub_addr = (uba_high | uba_low) >> UBA_ADDR_OFFSET;
	rg_entry->tpg_num = info->tpg_num;
	rg_entry->order_id = info->order_id;
	rg_entry->order_type = info->order_type;
	rg_entry->dst_eid_low = info->eid_low;
	rg_entry->dst_eid_high = info->eid_high;
	rg_entry->token_id = info->token_id;
	rg_entry->token_value = info->token_value;
	rg_entry->upi = info->upi;
	rg_entry->src_eid = info->src_eid;
}

static int handle_pgrg_table(struct ub_decoder *decoder, u64 *offset,
			     struct decoder_map_info *info, bool is_map)
{
	struct range_table_entry *rg_entry;
	struct page_table_entry *pg_entry;
	u32 table_idx, sub_index;
	int i;

	if (!is_map)
		return handle_page_table(decoder, offset, info, is_map);

	table_idx = get_pgtlb_idx(decoder, info->pa + *offset);
	sub_index = get_pgtlb_sub_idx(decoder, info->pa + *offset);
	pg_entry = (struct page_table_entry *)decoder->pgtlb.pgtlb_base +
		   table_idx * RGTLB_TO_PGTLB;
	if (sub_index != 0 || info->size - *offset < decoder->rg_size)
		return handle_page_table(decoder, offset, info, is_map);

	for (i = 0; i < RGTLB_TO_PGTLB; i++)
		if ((pg_entry + i)->next_lv_addr != decoder->invalid_page_dma)
			return handle_page_table(decoder, offset, info, is_map);

	ub_dbg(decoder->uent, "create range table\n");
	rg_entry = (struct range_table_entry *)pg_entry;
	fill_range_table(decoder, rg_entry, info, offset);
	return 0;
}

static int handle_table(struct ub_decoder *decoder,
			struct decoder_map_info *info, bool is_map)
{
	struct page_table *pgtlb = &decoder->pgtlb;
	struct range_table_entry *range_tlb_entry;
	u64 offset, rollback_size;
	u32 table_idx;
	int ret;

	for (offset = 0; offset < info->size;) {
		rollback_size = offset;

		mutex_lock(&decoder->table_lock);
		table_idx = get_pgtlb_idx(decoder, info->pa + offset);
		range_tlb_entry = (struct range_table_entry *)
				  pgtlb->pgtlb_base + table_idx;

		if (range_tlb_entry->entry_type == RANGE_TABLE)
			ret = handle_range_table(decoder, &offset, info,
						 is_map);
		else if (range_tlb_entry->next_lv_addr ==
			 decoder->invalid_page_dma)
			ret = handle_pgrg_table(decoder, &offset, info, is_map);
		else
			ret = handle_page_table(decoder, &offset, info, is_map);
		mutex_unlock(&decoder->table_lock);

		if (ret) {
			ub_err(decoder->uent, "decoder table occur fatal error ret=%d\n",
			       ret);
			/* if it is map operation, revert it. unmap operation can't revert */
			if (is_map)
				(void)ub_decoder_unmap(decoder, info->pa,
						       rollback_size);
			break;
		}
	}
	return ret;
}

int ub_decoder_unmap(struct ub_decoder *decoder, phys_addr_t addr, u64 size)
{
	int ret;
	struct decoder_map_info info = {
		.pa = addr,
		.size = size,
	};

	if (!decoder) {
		pr_err("unmap mmio decoder ptr is null\n");
		return -EINVAL;
	}
	if (size < SZ_1M)
		size = SZ_1M;
	ret = handle_table(decoder, &info, false);

	return ret;
}

int ub_decoder_map(struct ub_decoder *decoder, struct decoder_map_info *info)
{
	if (!decoder || !info) {
		pr_err("decoder or map info is null\n");
		return -EINVAL;
	}
	if (info->size < SZ_1M)
		info->size = SZ_1M;
	ub_info(decoder->uent,
		"decoder map, pa=%#llx, uba=%#llx, size=%#llx, cna=%#x, orderid=%#x, ordertype=%#x, eid_l=%#llx, eid_h=%#llx, upi=%#x src_eid=%#x\n",
		info->pa, info->uba, info->size, info->tpg_num, info->order_id,
		info->order_type, info->eid_low, info->eid_high, info->upi,
		info->src_eid);

	return handle_table(decoder, info, true);
}

void ub_decoder_init_page_table(struct ub_decoder *decoder, void *pgtlb_base)
{
	struct page_table_entry *pgtlb_entry;
	int i;

	for (i = 0; i < DECODER_PAGE_TABLE_SIZE; i++) {
		pgtlb_entry = (struct page_table_entry *)pgtlb_base + i;
		pgtlb_entry->entry_type = PAGE_TABLE;
		pgtlb_entry->next_lv_addr = decoder->invalid_page_dma;
		pgtlb_entry->pgtlb_attr = PGTLB_ATTR_DEFAULT;
	}
}

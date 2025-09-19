// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) HiSilicon Technologies Co., Ltd. 2025. All rights reserved.
 */

#define pr_fmt(fmt)	"ubus omm: " fmt

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

#define DECODER_PAGE_TABLE_LOC 35
#define DECODER_PAGE_TABLE_MASK GENMASK_ULL(43, 35)

#define get_pgtlb_idx(decoder, pa) ((((pa) - (decoder)->mmio_base_addr) & \
				    DECODER_PAGE_TABLE_MASK) >> \
				    DECODER_PAGE_TABLE_LOC)

static int handle_range_table(struct ub_decoder *decoder, u64 *offset,
			      struct decoder_map_info *info, bool is_map)
{
	return 0;
}

static int handle_page_table(struct ub_decoder *decoder, u64 *offset,
			     struct decoder_map_info *info, bool is_map)
{
	return 0;
}

static int handle_pgrg_table(struct ub_decoder *decoder, u64 *offset,
			     struct decoder_map_info *info, bool is_map)
{
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

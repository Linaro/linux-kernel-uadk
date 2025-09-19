/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) HiSilicon Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef __OMM_H__
#define __OMM_H__

struct decoder_map_info {
	phys_addr_t pa;
	phys_addr_t uba;
	u64 size;
	u32 tpg_num;
	u8 order_id;
	u8 order_type;
	u64 eid_low;
	u64 eid_high;
	u32 token_id;
	u32 token_value;
	u32 upi;
	u32 src_eid;
};

void ub_decoder_init_page_table(struct ub_decoder *decoder, void *pgtlb_base);
int ub_decoder_unmap(struct ub_decoder *decoder, phys_addr_t addr, u64 size);
int ub_decoder_map(struct ub_decoder *decoder, struct decoder_map_info *info);

#endif /* __OMM_H__ */

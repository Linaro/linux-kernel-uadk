/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) HiSilicon Technologies Co., Ltd. 2025. All rights reserved.
 */
#ifndef __EU_H__
#define __EU_H__

struct ub_eu_table { /* eid-upi table */
	dma_addr_t dma_addr;
	void *addr;
	size_t size;
	u32 entries;
	void *private_data; /* For ubc vendor private */
};

#pragma pack(2)
struct ub_eu_entry {
	u32 eid[SZ_4];
	u16 upi;
};
#pragma pack()

#define EU_ENTRY_SIZE 18

void ub_eu_table_init(struct ub_entity *uent);
void ub_eu_table_uninit(struct ub_entity *uent);
int ub_cfg_eu_table(struct ub_bus_controller *ubc, bool flag, u32 eid, u16 upi);

#endif /* __EU_H__ */

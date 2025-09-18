/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) HiSilicon Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef __UBRT_H__
#define __UBRT_H__

#include <linux/acpi.h>
#include <linux/types.h>

#define UB_TABLE_HEADER_NAME_LEN 16

struct ub_table_header {
	char name[UB_TABLE_HEADER_NAME_LEN];
	u32 total_size;
	u8 version;
	u8 reserved[3];
	u32 remaining_size;
	u32 checksum;
};

struct ubios_root_table {
	struct ub_table_header header;
	u16 count;
	u8 reserved[6];
	u64 sub_tables[];
};

struct ubrt_sub_table {
	u8 type;
	u8 reserved[7];
	u64 pointer;
};

struct acpi_table_ubrt {
	struct acpi_table_header head;
	u32 count;
	struct ubrt_sub_table sub_table[];
};

enum ubrt_sub_table_type {
	UB_BUS_CONTROLLER_TABLE = 0,
	UMMU_TABLE = 1,
	UB_RESERVED_MEMORY_TABLE = 2,
	VIRTUAL_BUS_TABLE = 3,
	CALL_ID_SERVICE_TABLE = 4,
	UB_ENTITY_TABLE = 5,
	UB_TOPOLOGY_TABLE = 6,
};

extern struct acpi_table_ubrt *acpi_table;
extern struct ubios_root_table *ubios_table;

void *ub_table_get(u64 pa);
void ub_table_put(void *va);

int handle_acpi_ubrt(void);
int handle_dts_ubrt(void);

void uninit_ub_nodes(void);

#endif /* __UBRT_H__ */

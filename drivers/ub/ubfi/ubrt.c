// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) HiSilicon Technologies Co., Ltd. 2025. All rights reserved.
 */

#define pr_fmt(fmt)	"ubfi ubrt: " fmt

#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/slab.h>

#include "ubrt.h"

#define UBIOS_SIG_UBC "ubc"

struct acpi_table_ubrt *acpi_table;
struct ubios_root_table *ubios_table;

/*
 * ummu max count is 32, max size is 40 + 32 * 128 = 4640
 * ubc max count is 32, max size is 40 + 88 + 32 * 256 + 32 * 4 = 8448
 */
#define UBIOS_TABLE_TOTLE_SIZE_MAX 8448

/* remember to use ub_table_put to release memory alloced by ub_table_get */
void *ub_table_get(u64 pa)
{
	void __iomem *va;
	u32 total_size;
	void *ret;

	if (!pa)
		return NULL;

	va = ioremap(pa, sizeof(struct ub_table_header));
	if (!va) {
		pr_err("ioremap ub table header failed\n");
		return NULL;
	}

	total_size = readl(va + UB_TABLE_HEADER_NAME_LEN);
	pr_debug("ub table size is[0x%x]\n", total_size);
	if (total_size == 0 || total_size > UBIOS_TABLE_TOTLE_SIZE_MAX) {
		pr_err("ubios table size is invalid\n");
		iounmap(va);
		return NULL;
	}
	iounmap(va);

	va = ioremap(pa, total_size);
	if (!va) {
		pr_err("ioremap full ub table failed\n");
		return NULL;
	}

	ret = kzalloc(total_size, GFP_KERNEL);
	if (!ret) {
		iounmap(va);
		return NULL;
	}

	memcpy_fromio(ret, va, total_size);
	iounmap(va);
	return ret;
}

void ub_table_put(void *va)
{
	kfree(va);
}

void uninit_ub_nodes(void)
{
}

int handle_acpi_ubrt(void)
{
	struct ubrt_sub_table *sub_table;
	int ret = 0;
	u32 i;

	pr_info("acpi ubrt sub table count is %u\n", acpi_table->count);

	for (i = 0; i < acpi_table->count; i++) {
		sub_table = &acpi_table->sub_table[i];
		switch (sub_table->type) {
		case UB_BUS_CONTROLLER_TABLE:
			break;
		default:
			pr_warn("Ignore sub table: type %u\n", sub_table->type);
			break;
		}
		if (ret) {
			pr_err("parse ubrt sub table type %u failed\n",
				sub_table->type);
			goto fail;
		}
	}
	return ret;
fail:
	uninit_ub_nodes();
	return ret;
}

int handle_dts_ubrt(void)
{
	char name[UB_TABLE_HEADER_NAME_LEN] = {};
	struct ub_table_header *header;
	int ret = 0, i;

	if (ubios_table->count == 0) {
		pr_err("ubios root table has no sub tables.\n");
		return 0;
	}
	pr_info("ubios sub table count is %u\n", ubios_table->count);

	for (i = 0; i < ubios_table->count; i++) {
		header = (struct ub_table_header *)ub_table_get(
			 ubios_table->sub_tables[i]);
		if (!header)
			continue;

		memcpy(name, header->name, UB_TABLE_HEADER_NAME_LEN - 1);
		pr_info("ubrt sub table name is %s\n", name);
		ub_table_put(header);

		if (!strncmp(name, UBIOS_SIG_UBC, strlen(UBIOS_SIG_UBC)))
			ret = 0;
		else
			pr_warn("Ignore sub table: %s\n", name);

		if (ret) {
			pr_err("Create %s device ret=%d\n", name, ret);
			goto out;
		}
	}

	return 0;

out:
	uninit_ub_nodes();
	return ret;
}

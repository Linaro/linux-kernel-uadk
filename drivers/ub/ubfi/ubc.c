// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) HiSilicon Technologies Co., Ltd. 2025. All rights reserved.
 */

#define pr_fmt(fmt)	"ubfi ubc: " fmt

#include <linux/acpi.h>
#include <linux/errno.h>
#include <linux/io.h>
#include <linux/irqdomain.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/types.h>
#include <ub/ubus/ubus.h>

#include "ubrt.h"
#include "ub_fi.h"
#include "ubc.h"

struct list_head ubc_list;
EXPORT_SYMBOL_GPL(ubc_list);

u32 ubc_eid_start;
EXPORT_SYMBOL_GPL(ubc_eid_start);

u32 ubc_eid_end;
EXPORT_SYMBOL_GPL(ubc_eid_end);

u32 ubc_cna_start;
EXPORT_SYMBOL_GPL(ubc_cna_start);

u32 ubc_cna_end;
EXPORT_SYMBOL_GPL(ubc_cna_end);

u8 ubc_feature;
EXPORT_SYMBOL_GPL(ubc_feature);

static bool cluster_mode;

static int acpi_update_ubc_msi_domain(void)
{
	return 0;
}

static int dts_update_ubc_msi_domain(void)
{
	return 0;
}

static int create_ubc(struct ubc_node *node, u32 ctl_no)
{
	return 0;
}

static int parse_ubc_table(void *info_node)
{
	struct ubrt_ubc_table *ubc_table = info_node;
	struct ubc_node *node = ubc_table->ubcs;
	u32 count, i;
	int ret;

	count = ubc_table->ubc_count;
	if (!count) {
		pr_warn("ubc table has no ubc.\n");
		return 0;
	}

	/* get ubc common attribute */
	ubc_cna_start = ubc_table->cna_start;
	ubc_cna_end = ubc_table->cna_end;
	ubc_eid_start = ubc_table->eid_start;
	ubc_eid_end = ubc_table->eid_end;
	ubc_feature = ubc_table->feature;
	cluster_mode = ubc_table->cluster_mode;

	pr_info("cna_start=%u, cna_end=%u\n", ubc_cna_start, ubc_cna_end);
	pr_info("eid_start=%u, eid_end=%u\n", ubc_eid_start, ubc_eid_end);
	pr_info("ubc_count=%u, bios_cluster_mode=%u, feature=%u\n", count,
		cluster_mode, ubc_feature);
	if (ubc_cna_start > ubc_cna_end || ubc_eid_start > ubc_eid_end) {
		pr_err("eid or cna range is incorrect\n");
		return -EINVAL;
	}

	for (i = 0; i < count; i++) {
		ret = create_ubc(node, i);
		if (ret) {
			pr_err("Create No.%u ubc failed, ret=%d\n", i, ret);
			return ret;
		}
		node++;
	}

	return 0;
}

static void ub_destroy_bus_controllers(void)
{
}

void destroy_ubc(void)
{
	ub_destroy_bus_controllers();
}

int handle_ubc_table(u64 pointer)
{
	void *info_node = ub_table_get(pointer);
	int ret;

	if (!info_node)
		return -EINVAL;

	INIT_LIST_HEAD(&ubc_list);

	ret = parse_ubc_table(info_node);
	if (ret)
		goto err_handle;

	pr_info("Update msi domain for ub bus controller\n");
	/* Update msi domain for ub bus controller */
	if (bios_mode == ACPI)
		ret = acpi_update_ubc_msi_domain();
	else
		ret = dts_update_ubc_msi_domain();

	if (ret)
		goto err_handle;

	ub_table_put(info_node);
	return 0;

err_handle:
	ub_table_put(info_node);
	destroy_ubc();
	return ret;
}

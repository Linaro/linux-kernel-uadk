// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) HiSilicon Technologies Co., Ltd. 2025. All rights reserved.
 */

#define pr_fmt(fmt)	"ubus cna: " fmt

#include "ubus.h"
#include "port.h"
#include "cna.h"

static DEFINE_IDA(ub_cna_ida);

static int ub_alloc_cna_id(void)
{
	return ida_alloc_range(&ub_cna_ida, ubc_cna_start, ubc_cna_end, GFP_KERNEL);
}

static void ub_free_cna_id(u32 cna)
{
	ida_free(&ub_cna_ida, cna);
}

void ub_cna_free(struct ub_entity *uent)
{
	struct ub_port *port;

	if (!is_primary(uent)) {
		ub_warn(uent, "only entity0 need to free cna\n");
		return;
	}

	for_each_uent_port(port, uent) {
		if (port->domain_boundary && is_ibus_controller(uent))
			continue;

		if (port->cna) {
			if (!ONE_CNA(uent))
				ub_free_cna_id(port->cna);
			port->cna = 0;
		}
	}

	if (is_ibus_controller(uent) && uent->ubc->cluster)
		return;

	if (uent->cna) {
		ub_free_cna_id(uent->cna);
		uent->cna = 0;
	}
}

int ub_cna_alloc(struct ub_entity *uent)
{
	bool one_cna = ONE_CNA(uent);
	struct ub_port *port;
	int ret;

	if (!is_primary(uent)) {
		ub_warn(uent, "only entity0 need to alloc cna\n");
		return 0;
	}

	if (!(is_ibus_controller(uent) && uent->ubc->cluster)) {
		ret = ub_alloc_cna_id();
		if (ret < 0)
			return ret;

		uent->cna = (u32)ret;
	}

	for_each_uent_port(port, uent) {
		if (is_ibus_controller(uent) && port->domain_boundary)
			continue;
		if (one_cna) {
			port->cna = uent->cna;
		} else {
			ret = ub_alloc_cna_id();
			if (ret < 0)
				return ret;
			port->cna = (u32)ret;
		}
	}

	return 0;
}

// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) HiSilicon Technologies Co., Ltd. 2025. All rights reserved.
 */

#define pr_fmt(fmt)	"ubus entity: " fmt

#include "ubus.h"

struct ub_entity *ub_alloc_ent(void)
{
	return NULL;
}
EXPORT_SYMBOL_GPL(ub_alloc_ent);

int ub_setup_ent(struct ub_entity *uent)
{
	return -EINVAL;
}
EXPORT_SYMBOL_GPL(ub_setup_ent);

void ub_entity_add(struct ub_entity *uent, void *ctx)
{
}
EXPORT_SYMBOL_GPL(ub_entity_add);

void ub_start_ent(struct ub_entity *uent)
{
}
EXPORT_SYMBOL_GPL(ub_start_ent);

void ub_stop_entities(void)
{
}

void ub_remove_entities(void)
{
}

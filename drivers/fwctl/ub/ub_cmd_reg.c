// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved.
 */

#include "ub_cmd_reg.h"

static struct ubctl_func_dispatch g_ubctl_query_reg[] = {
	{ UTOOL_CMD_QUERY_MAX, NULL, NULL }
};

struct ubctl_func_dispatch *ubctl_get_query_reg_func(struct ubctl_dev *ucdev,
						     u32 rpc_cmd)
{
	u32 i;

	if (!ucdev)
		return NULL;

	for (i = 0; i < ARRAY_SIZE(g_ubctl_query_reg); i++) {
		if (g_ubctl_query_reg[i].rpc_cmd == rpc_cmd)
			return &g_ubctl_query_reg[i];
	}

	return NULL;
}

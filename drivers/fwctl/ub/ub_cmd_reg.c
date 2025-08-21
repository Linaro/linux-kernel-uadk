// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved.
 */

#include "ub_cmdq.h"
#include "ub_cmd_reg.h"

static int ubctl_query_nl_pkt_stats_data(struct ubctl_dev *ucdev,
					 struct ubctl_query_cmd_param *query_cmd_param,
					 struct ubctl_func_dispatch *query_func)
{
	struct ubctl_query_dp query_dp[] = {
		{ UBCTL_QUERY_NL_PKT_STATS_DFX, UBCTL_NL_PKT_STATS_LEN, UBCTL_READ, NULL, 0 },
	};

	return ubctl_query_data(ucdev, query_cmd_param, query_func,
				query_dp, ARRAY_SIZE(query_dp));
}

static struct ubctl_func_dispatch g_ubctl_query_reg[] = {
	{ UTOOL_CMD_QUERY_NL_PKT_STATS, ubctl_query_nl_pkt_stats_data,
	  ubctl_query_data_deal },

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

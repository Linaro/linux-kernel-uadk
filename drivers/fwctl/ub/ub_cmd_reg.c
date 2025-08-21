// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved.
 */

#include "ub_cmdq.h"
#include "ub_cmd_reg.h"

static int ubctl_query_nl_data(struct ubctl_dev *ucdev,
			       struct ubctl_query_cmd_param *query_cmd_param,
			       struct ubctl_func_dispatch *query_func)
{
	struct ubctl_query_dp query_dp[] = {
		{ UBCTL_QUERY_NL_PKT_STATS_DFX, UBCTL_NL_PKT_STATS_LEN, UBCTL_READ, NULL, 0 },
		{ UBCTL_QUERY_NL_SSU_STATS_DFX, UBCTL_NL_SSU_STATS_LEN, UBCTL_READ, NULL, 0 },
		{ UBCTL_QUERY_NL_ABN_DFX, UBCTL_NL_ABN_LEN, UBCTL_READ, NULL, 0 },
	};

	return ubctl_query_data(ucdev, query_cmd_param, query_func,
				query_dp, ARRAY_SIZE(query_dp));
}

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

static int ubctl_query_nl_ssu_stats_data(struct ubctl_dev *ucdev,
					 struct ubctl_query_cmd_param *query_cmd_param,
					 struct ubctl_func_dispatch *query_func)
{
	struct ubctl_query_dp query_dp[] = {
		{ UBCTL_QUERY_NL_SSU_STATS_DFX, UBCTL_NL_SSU_STATS_LEN, UBCTL_READ, NULL, 0 },
	};

	return ubctl_query_data(ucdev, query_cmd_param, query_func,
				query_dp, ARRAY_SIZE(query_dp));
}

static int ubctl_query_nl_abn_data(struct ubctl_dev *ucdev,
				   struct ubctl_query_cmd_param *query_cmd_param,
				   struct ubctl_func_dispatch *query_func)
{
	struct ubctl_query_dp query_dp[] = {
		{ UBCTL_QUERY_NL_ABN_DFX, UBCTL_NL_ABN_LEN, UBCTL_READ, NULL, 0 },
	};

	return ubctl_query_data(ucdev, query_cmd_param, query_func,
				query_dp, ARRAY_SIZE(query_dp));
}

static int ubctl_query_dl_data(struct ubctl_dev *ucdev,
			       struct ubctl_query_cmd_param *query_cmd_param,
			       struct ubctl_func_dispatch *query_func)
{
	struct ubctl_query_dp query_dp[] = {
		{ UBCTL_QUERY_DL_PKT_STATS_DFX, UBCTL_DL_PKT_STATS_LEN, UBCTL_READ, NULL, 0 },
		{ UBCTL_QUERY_DL_REPL_DFX, UBCTL_DL_REPL_LEN, UBCTL_READ, NULL, 0 },
		{ UBCTL_QUERY_DL_LINK_STATUS_DFX, UBCTL_DL_LINK_STATUS_LEN, UBCTL_READ, NULL, 0 },
		{ UBCTL_QUERY_DL_LANE_DFX, UBCTL_DL_LANE_LEN, UBCTL_READ, NULL, 0 },
		{ UBCTL_QUERY_DL_BIT_ERR_DFX, UBCTL_DL_BIT_ERR_LEN, UBCTL_READ, NULL, 0 },
	};

	return ubctl_query_data(ucdev, query_cmd_param, query_func,
				query_dp, ARRAY_SIZE(query_dp));
}

static int ubctl_query_dl_pkt_stats_data(struct ubctl_dev *ucdev,
					 struct ubctl_query_cmd_param *query_cmd_param,
					 struct ubctl_func_dispatch *query_func)
{
	struct ubctl_query_dp query_dp[] = {
		{ UBCTL_QUERY_DL_PKT_STATS_DFX, UBCTL_DL_PKT_STATS_LEN, UBCTL_READ, NULL, 0 },
		{ UBCTL_QUERY_DL_REPL_DFX, UBCTL_DL_REPL_LEN, UBCTL_READ, NULL, 0 },
	};

	return ubctl_query_data(ucdev, query_cmd_param, query_func,
				query_dp, ARRAY_SIZE(query_dp));
}

static int ubctl_query_dl_link_status_data(struct ubctl_dev *ucdev,
					   struct ubctl_query_cmd_param *query_cmd_param,
					   struct ubctl_func_dispatch *query_func)
{
	struct ubctl_query_dp query_dp[] = {
		{ UBCTL_QUERY_DL_LINK_STATUS_DFX, UBCTL_DL_LINK_STATUS_LEN, UBCTL_READ, NULL, 0 },
	};

	return ubctl_query_data(ucdev, query_cmd_param, query_func,
				query_dp, ARRAY_SIZE(query_dp));
}

static int ubctl_query_dl_lane_data(struct ubctl_dev *ucdev,
				    struct ubctl_query_cmd_param *query_cmd_param,
				    struct ubctl_func_dispatch *query_func)
{
	struct ubctl_query_dp query_dp[] = {
		{ UBCTL_QUERY_DL_LANE_DFX, UBCTL_DL_LANE_LEN, UBCTL_READ, NULL, 0 },
	};

	return ubctl_query_data(ucdev, query_cmd_param, query_func,
				query_dp, ARRAY_SIZE(query_dp));
}

static int ubctl_query_dl_bit_err_data(struct ubctl_dev *ucdev,
				       struct ubctl_query_cmd_param *query_cmd_param,
				       struct ubctl_func_dispatch *query_func)
{
	struct ubctl_query_dp query_dp[] = {
		{ UBCTL_QUERY_DL_BIT_ERR_DFX, UBCTL_DL_BIT_ERR_LEN, UBCTL_READ, NULL, 0 },
	};

	return ubctl_query_data(ucdev, query_cmd_param, query_func,
				query_dp, ARRAY_SIZE(query_dp));
}

static int ubctl_query_dl_bist_data(struct ubctl_dev *ucdev,
				    struct ubctl_query_cmd_param *query_cmd_param,
				    struct ubctl_func_dispatch *query_func)
{
	struct ubctl_query_dp query_dp[] = {
		{ UBCTL_QUERY_CONF_DL_BIST_DFX, UBCTL_DL_BIST_LEN, UBCTL_READ, NULL, 0 },
	};

	return ubctl_query_data(ucdev, query_cmd_param, query_func,
				query_dp, ARRAY_SIZE(query_dp));
}

static int ubctl_conf_dl_bist_data(struct ubctl_dev *ucdev,
				   struct ubctl_query_cmd_param *query_cmd_param,
				   struct ubctl_func_dispatch *query_func)
{
	struct ubctl_query_dp query_dp[] = {
		{ UBCTL_QUERY_CONF_DL_BIST_DFX, UBCTL_DL_BIST_LEN, UBCTL_WRITE, NULL, 0 },
	};

	return ubctl_query_data(ucdev, query_cmd_param, query_func,
				query_dp, ARRAY_SIZE(query_dp));
}

static int ubctl_query_dl_bist_err_data(struct ubctl_dev *ucdev,
					struct ubctl_query_cmd_param *query_cmd_param,
					struct ubctl_func_dispatch *query_func)
{
	struct ubctl_query_dp query_dp[] = {
		{ UBCTL_QUERY_DL_BIST_ERR_DFX, UBCTL_DL_BIST_ERR_LEN, UBCTL_READ, NULL, 0 },
	};

	return ubctl_query_data(ucdev, query_cmd_param, query_func,
				query_dp, ARRAY_SIZE(query_dp));
}

static int ubctl_query_ta_data(struct ubctl_dev *ucdev,
			       struct ubctl_query_cmd_param *query_cmd_param,
			       struct ubctl_func_dispatch *query_func)
{
	struct ubctl_query_dp query_dp[] = {
		{ UBCTL_QUERY_TA_PKT_STATS_DFX, UBCTL_TA_PKT_STATS_LEN, UBCTL_READ, NULL, 0 },
		{ UBCTL_QUERY_TA_ABN_STATS_DFX, UBCTL_TA_ABN_STATS_LEN, UBCTL_READ, NULL, 0 },
	};

	return ubctl_query_data(ucdev, query_cmd_param, query_func,
				query_dp, ARRAY_SIZE(query_dp));
}

static int ubctl_query_ta_pkt_stats(struct ubctl_dev *ucdev,
				    struct ubctl_query_cmd_param *query_cmd_param,
				    struct ubctl_func_dispatch *query_func)
{
	struct ubctl_query_dp query_dp[] = {
		{ UBCTL_QUERY_TA_PKT_STATS_DFX, UBCTL_TA_PKT_STATS_LEN, UBCTL_READ, NULL, 0 },
	};

	return ubctl_query_data(ucdev, query_cmd_param, query_func,
				query_dp, ARRAY_SIZE(query_dp));
}

static int ubctl_query_ta_abn_stats(struct ubctl_dev *ucdev,
				    struct ubctl_query_cmd_param *query_cmd_param,
				    struct ubctl_func_dispatch *query_func)
{
	struct ubctl_query_dp query_dp[] = {
		{ UBCTL_QUERY_TA_ABN_STATS_DFX, UBCTL_TA_ABN_STATS_LEN, UBCTL_READ, NULL, 0 },
	};

	return ubctl_query_data(ucdev, query_cmd_param, query_func,
				query_dp, ARRAY_SIZE(query_dp));
}

static struct ubctl_func_dispatch g_ubctl_query_reg[] = {
	{ UTOOL_CMD_QUERY_NL, ubctl_query_nl_data, ubctl_query_data_deal },
	{ UTOOL_CMD_QUERY_NL_PKT_STATS, ubctl_query_nl_pkt_stats_data,
	  ubctl_query_data_deal },
	{ UTOOL_CMD_QUERY_NL_SSU_STATS, ubctl_query_nl_ssu_stats_data,
	  ubctl_query_data_deal },
	{ UTOOL_CMD_QUERY_NL_ABN, ubctl_query_nl_abn_data, ubctl_query_data_deal },

	{ UTOOL_CMD_QUERY_DL, ubctl_query_dl_data, ubctl_query_data_deal },
	{ UTOOL_CMD_QUERY_DL_PKT_STATS, ubctl_query_dl_pkt_stats_data,
	  ubctl_query_data_deal },
	{ UTOOL_CMD_QUERY_DL_LINK_STATUS, ubctl_query_dl_link_status_data,
	  ubctl_query_data_deal },
	{ UTOOL_CMD_QUERY_DL_LANE, ubctl_query_dl_lane_data,
	  ubctl_query_data_deal },
	{ UTOOL_CMD_QUERY_DL_BIT_ERR, ubctl_query_dl_bit_err_data,
	  ubctl_query_data_deal },
	{ UTOOL_CMD_QUERY_DL_BIST, ubctl_query_dl_bist_data,
	  ubctl_query_data_deal },
	{ UTOOL_CMD_CONF_DL_BIST, ubctl_conf_dl_bist_data,
	  ubctl_query_data_deal },
	{ UTOOL_CMD_QUERY_DL_BIST_ERR, ubctl_query_dl_bist_err_data,
	  ubctl_query_data_deal },

	{ UTOOL_CMD_QUERY_TA, ubctl_query_ta_data, ubctl_query_data_deal },
	{ UTOOL_CMD_QUERY_TA_PKT_STATS, ubctl_query_ta_pkt_stats,
	  ubctl_query_data_deal },
	{ UTOOL_CMD_QUERY_TA_ABN_STATS, ubctl_query_ta_abn_stats,
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

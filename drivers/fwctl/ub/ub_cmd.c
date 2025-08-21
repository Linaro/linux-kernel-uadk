// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved.
 */

#include "ub_cmdq.h"
#include "ub_cmd.h"

struct ubctl_query_trace {
	u32 port_id;
	u32 index;
	u32 cur_count;
	u32 total_count;
	u32 data[];
};

static int ubctl_trace_data_deal(struct ubctl_dev *ucdev,
				 struct ubctl_query_cmd_param *query_cmd_param,
				 struct ubctl_cmd *cmd, u32 out_len, u32 offset)
{
#define UBCTL_TRACE_SIZE 4U
#define UBCTL_TOTAL_CNT_MAX 64U

	struct fwctl_rpc_ub_out *trace_out = query_cmd_param->out;
	struct ubctl_query_trace *trace_info = cmd->out_data;
	u32 trace_max_len = query_cmd_param->out_len;
	u32 pos_index = offset * UBCTL_TRACE_SIZE;

	if ((trace_info->total_count > UBCTL_TOTAL_CNT_MAX) ||
	    (trace_info->total_count * UBCTL_TRACE_SIZE >= trace_max_len) ||
	    (pos_index >= trace_max_len || cmd->out_len < sizeof(struct ubctl_query_trace))) {
		ubctl_err(ucdev, "cmd out data length is error.\n");
		return -EINVAL;
	}

	if (pos_index == 0)
		memcpy(trace_out->data, cmd->out_data, cmd->out_len);
	else
		memcpy((u32 *)(trace_out->data) + pos_index, trace_info->data,
		       cmd->out_len - sizeof(struct ubctl_query_trace));

	trace_out->data_size = query_cmd_param->out_len;
	return 0;
}

static int ubctl_send_deal_trace(struct ubctl_dev *ucdev,
				 struct ubctl_query_cmd_param *query_cmd_param,
				 struct ubctl_query_cmd_dp *cmd_data, u32 offset)
{
	u32 out_len = UBCTL_DL_TRACE_LEN;
	struct ubctl_cmd cmd = {};
	int ret = 0;

	if (!cmd_data->query_func->data_deal) {
		ubctl_err(ucdev, "ubctl data deal func is null.\n");
		return -EINVAL;
	}

	cmd.op_code = UBCTL_QUERY_DL_TRACE_DFX;

	ret = ubctl_fill_cmd(&cmd, cmd_data->cmd_in, cmd_data->cmd_out,
			     out_len, UBCTL_READ);
	if (ret) {
		ubctl_err(ucdev, "ubctl fill cmd failed.\n");
		return ret;
	}

	ret = ubctl_ubase_cmd_send(ucdev->adev, &cmd);
	if (ret) {
		ubctl_err(ucdev, "ubctl ubase cmd send failed, ret = %d.\n", ret);
		return -EINVAL;
	}

	ret = cmd_data->query_func->data_deal(ucdev, query_cmd_param, &cmd,
					      out_len, offset);
	if (ret)
		ubctl_err(ucdev, "ubctl data deal failed, ret = %d.\n", ret);

	return ret;
}

static int ubctl_query_dl_trace_data(struct ubctl_dev *ucdev,
				     struct ubctl_query_cmd_param *query_cmd_param,
				     struct ubctl_func_dispatch *query_func)
{
	struct fwctl_pkt_in_port *pkt_in = (struct fwctl_pkt_in_port *)query_cmd_param->in->data;
	u32 trace_index = 0, offset = 0, expect_total = 0, out_len = UBCTL_DL_TRACE_LEN, tmp_sum;
	struct ubctl_query_cmd_dp cmd_dp = {};
	int ret = 0;

	if (query_cmd_param->in->data_size != sizeof(struct fwctl_pkt_in_port)) {
		ubctl_err(ucdev, "user data of trace is invalid.\n");
		return -EINVAL;
	}

	while (1) {
		struct ubctl_query_trace *cmd_in __free(kvfree) = kvzalloc(out_len, GFP_KERNEL);
		if (!cmd_in)
			return -ENOMEM;

		struct ubctl_query_trace *cmd_out __free(kvfree) = kvzalloc(out_len, GFP_KERNEL);
		if (!cmd_out)
			return -ENOMEM;

		cmd_in->index = trace_index;
		cmd_in->port_id = pkt_in->port_id;

		cmd_dp = (struct ubctl_query_cmd_dp) {
			.cmd_in = cmd_in,
			.cmd_out = cmd_out,
			.query_func = query_func,
		};

		ret = ubctl_send_deal_trace(ucdev, query_cmd_param,
					    &cmd_dp, offset);
		if (ret)
			return ret;

		offset = cmd_out->cur_count + 1;
		trace_index = cmd_out->cur_count;
		tmp_sum = cmd_out->cur_count + cmd_in->index;

		if ((tmp_sum <= expect_total) || (tmp_sum > cmd_out->total_count)) {
			ubctl_err(ucdev, "software data of trace is invalid.\n");
			return -EINVAL;
		}

		if (tmp_sum == cmd_out->total_count)
			break;

		expect_total = tmp_sum;
	}

	return ret;
}

static struct ubctl_func_dispatch g_ubctl_query_func[] = {
	{ UTOOL_CMD_QUERY_DL_LINK_TRACE, ubctl_query_dl_trace_data,
	  ubctl_trace_data_deal },

	{ UTOOL_CMD_QUERY_MAX, NULL, NULL }
};

struct ubctl_func_dispatch *ubctl_get_query_func(struct ubctl_dev *ucdev, u32 rpc_cmd)
{
	u32 i;

	if (!ucdev)
		return NULL;

	for (i = 0; i < ARRAY_SIZE(g_ubctl_query_func); i++) {
		if (g_ubctl_query_func[i].rpc_cmd == rpc_cmd)
			return &g_ubctl_query_func[i];
	}

	return NULL;
}

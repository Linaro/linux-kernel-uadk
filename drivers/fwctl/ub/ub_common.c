// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2025 HiSilicon Technologies Co., Ltd. 2025-2025. All rights reserved.
 */

#include <ub/ubase/ubase_comm_cmd.h>

#include "ub_common.h"

static inline void ubctl_struct_cpu_to_le32(u32 *data, u32 cnt)
{
	for (u32 i = 0; i < cnt; i++)
		data[i] = cpu_to_le32(data[i]);
}

static inline void ubctl_struct_le32_to_cpu(u32 *data, u32 cnt)
{
	for (u32 i = 0; i < cnt; i++)
		data[i] = le32_to_cpu(data[i]);
}

static inline int ubctl_ubase_cmd_send_param_check(struct auxiliary_device *adev,
						   struct ubctl_cmd *cmd)
{
	if (!adev || !cmd)
		return -EINVAL;

	if (!cmd->in_data || !cmd->out_data)
		return -EINVAL;

	return 0;
}

int ubctl_ubase_cmd_send(struct auxiliary_device *adev, struct ubctl_cmd *cmd)
{
	struct ubase_cmd_buf in, out;
	int ret;

	if (ubctl_ubase_cmd_send_param_check(adev, cmd))
		return -EINVAL;

	ubctl_struct_cpu_to_le32(cmd->in_data, cmd->in_len / sizeof(u32));
	ubase_fill_inout_buf(&in, cmd->op_code, cmd->is_read, cmd->in_len,
			     cmd->in_data);
	ubase_fill_inout_buf(&out, cmd->op_code, cmd->is_read, cmd->out_len,
			     cmd->out_data);

	ret = ubase_cmd_send_inout(adev, &in, &out);
	if (ret)
		return ret;

	ubctl_struct_le32_to_cpu(cmd->out_data, cmd->out_len / sizeof(u32));

	return 0;
}

int ubctl_fill_cmd(struct ubctl_cmd *cmd, void *cmd_in, void *cmd_out,
		   u32 out_len, u32 is_read)
{
	if (!cmd || !cmd_in || !cmd_out)
		return -EINVAL;

	cmd->is_read = is_read;
	cmd->in_data = cmd_in;
	cmd->out_data = cmd_out;
	cmd->in_len = out_len;
	cmd->out_len = out_len;

	return 0;
}

static int ubctl_query_param_check(struct ubctl_dev *ucdev,
				   struct ubctl_query_cmd_param *query_cmd_param,
				   struct ubctl_func_dispatch *query_func,
				   struct ubctl_query_dp *query_dp)
{
	if (!ucdev || !query_cmd_param || !query_func || !query_dp)
		return -EINVAL;

	if (!query_cmd_param->in || !query_cmd_param->out) {
		ubctl_err(ucdev, "ubctl in or out is null.\n");
		return -EINVAL;
	}

	if (!query_func->data_deal) {
		ubctl_err(ucdev, "ubctl data deal func is null.\n");
		return -EINVAL;
	}

	return 0;
}

static int ubctl_cmd_send_deal(struct ubctl_dev *ucdev,
			       struct ubctl_query_cmd_param *query_cmd_param,
			       struct ubctl_query_dp *query_dp,
			       struct ubctl_query_cmd_dp *cmd_data, u32 offset)
{
	int *retval = &query_cmd_param->out->retval;
	struct ubctl_cmd cmd = {};
	int ret = 0;

	cmd.op_code = query_dp->op_code;
	ret = ubctl_fill_cmd(&cmd, cmd_data->cmd_in, cmd_data->cmd_out,
			     query_dp->out_len, query_dp->is_read);
	if (ret) {
		ubctl_err(ucdev, "ubctl fill cmd failed.\n");
		return ret;
	}

	*retval = ubctl_ubase_cmd_send(ucdev->adev, &cmd);
	if (*retval) {
		ubctl_err(ucdev, "ubctl ubase cmd send failed, retval = %d.\n",
			  *retval);
		return -EINVAL;
	}

	ret = cmd_data->query_func->data_deal(ucdev, query_cmd_param, &cmd,
					      query_dp->out_len, offset);
	if (ret)
		ubctl_err(ucdev, "ubctl data deal failed, ret = %d.\n", ret);

	return ret;
}

int ubctl_query_data(struct ubctl_dev *ucdev,
		     struct ubctl_query_cmd_param *query_cmd_param,
		     struct ubctl_func_dispatch *query_func,
		     struct ubctl_query_dp *query_dp, u32 query_dp_num)
{
	u32 offset = 0;
	int ret = 0;
	u32 i;

	ret = ubctl_query_param_check(ucdev, query_cmd_param, query_func, query_dp);
	if (ret) {
		ubctl_err(ucdev, "ubctl query param check failed, ret = %d.\n", ret);
		return ret;
	}

	for (i = 0; i < query_dp_num; i++) {
		if (query_cmd_param->in->data_size > query_dp[i].out_len) {
			ubctl_err(ucdev, "ubctl in data size is bigger than out len.\n");
			return -EINVAL;
		}

		void *cmd_in __free(kvfree) = kvzalloc(query_dp[i].out_len, GFP_KERNEL);
		if (!cmd_in)
			return -ENOMEM;

		void *cmd_out __free(kvfree) = kvzalloc(query_dp[i].out_len, GFP_KERNEL);
		if (!cmd_out)
			return -ENOMEM;

		struct ubctl_query_cmd_dp cmd_dp = (struct ubctl_query_cmd_dp) {
			.cmd_in = cmd_in,
			.cmd_out = cmd_out,
			.query_func = query_func,
		};

		memcpy(cmd_dp.cmd_in, query_cmd_param->in->data, query_cmd_param->in->data_size);
		ret = ubctl_cmd_send_deal(ucdev, query_cmd_param, &query_dp[i],
					  &cmd_dp, offset);
		if (ret)
			return ret;

		offset += query_dp[i].out_len / sizeof(u32);
	}
	return 0;
}

int ubctl_query_data_deal(struct ubctl_dev *ucdev,
			  struct ubctl_query_cmd_param *query_cmd_param,
			  struct ubctl_cmd *cmd, u32 out_len, u32 offset)
{
	if (!ucdev || !query_cmd_param || !cmd)
		return -EINVAL;

	if (!query_cmd_param->in || !query_cmd_param->out) {
		ubctl_err(ucdev, "ubctl in or out is null.\n");
		return -EINVAL;
	}

	if (cmd->out_len != out_len) {
		ubctl_err(ucdev, "out data size is not equal to out len.\n");
		return -EINVAL;
	}

	if ((offset * (u32)sizeof(u32) + out_len) > query_cmd_param->out_len) {
		ubctl_err(ucdev, "offset size is bigger than user out len.\n");
		return -EINVAL;
	}

	memcpy(&query_cmd_param->out->data[offset], cmd->out_data, cmd->out_len);
	query_cmd_param->out->data_size += cmd->out_len;

	return 0;
}

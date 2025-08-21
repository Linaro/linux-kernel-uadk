// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved.
 */

#include "ub_cmdq.h"
#include "ub_cmd.h"

#define UBCTL_SCC_SZ_1M 0x100000

struct ubctl_query_trace {
	u32 port_id;
	u32 index;
	u32 cur_count;
	u32 total_count;
	u32 data[];
};

struct ubctl_scc_data {
	u32 phy_addr_low;
	u32 phy_addr_high;
	u32 data_size;
	u32 rsv[3];
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

static int ubctl_scc_data_deal(struct ubctl_dev *ucdev, u32 index,
			       struct fwctl_rpc_ub_out *out,
			       struct ubctl_scc_data *scc)
{
#define UBCTL_SCC_OUT_LEN ((UBCTL_SCC_SZ_1M) / (sizeof(u32)))
#define UBCTL_SCC_INDEX_MAX_NUM 1

	u32 scc_data_len = scc->data_size / sizeof(u32);
	u32 data_len = out->data_size / sizeof(u32);
	u32 offset = index * UBCTL_SCC_OUT_LEN;
	u32 *scc_data = out->data;
	void __iomem *vir_addr;
	u64 phy_addr;
	u32 i, j;

	if (index > UBCTL_SCC_INDEX_MAX_NUM) {
		ubctl_err(ucdev, "scc index is invalid, index = %u.\n", index);
		return -EINVAL;
	}

	phy_addr = UBCTL_GET_PHY_ADDR(scc->phy_addr_high, scc->phy_addr_low);

	vir_addr = ioremap(phy_addr, scc->data_size);
	if (!vir_addr) {
		ubctl_err(ucdev, "addr ioremap failed.\n");
		return -EFAULT;
	}

	for (i = offset, j = 0; i < scc_data_len && j < data_len; i++, j++)
		scc_data[j] = readl(vir_addr + i * sizeof(u32));

	iounmap(vir_addr);
	return 0;
}

static int ubctl_scc_data_len_check(struct ubctl_dev *ucdev, u32 out_len,
				    u32 data_size, u32 scc_len)
{
#define UBCTL_SCC_CACHE 0x200000

	if (data_size != UBCTL_SCC_CACHE) {
		ubctl_err(ucdev, "scc data size is not equal to 2M, data size = %u.\n",
			  data_size);
		return -EINVAL;
	}

	if (out_len != scc_len) {
		ubctl_err(ucdev, "scc out len is invalid, out len = %u.\n",
			  out_len);
		return -EINVAL;
	}

	return 0;
}

static int ubctl_scc_version_deal(struct ubctl_dev *ucdev,
				  struct ubctl_query_cmd_param *query_cmd_param,
				  struct ubctl_cmd *cmd, u32 out_len, u32 offset)
{
#define UBCTL_SCC_VERSION_SZ 24

	struct fwctl_pkt_in_index *pkt_in = NULL;
	struct ubctl_scc_data *scc = NULL;
	int ret = 0;

	if (query_cmd_param->in->data_size != sizeof(struct fwctl_pkt_in_index)) {
		ubctl_err(ucdev, "user data of scc version is invalid.\n");
		return -EINVAL;
	}
	pkt_in = (struct fwctl_pkt_in_index *)query_cmd_param->in->data;
	scc = (struct ubctl_scc_data *)cmd->out_data;

	ret = ubctl_scc_data_len_check(ucdev, query_cmd_param->out_len,
				       scc->data_size, UBCTL_SCC_VERSION_SZ);
	if (ret) {
		ubctl_err(ucdev, "scc version data len check failed, ret = %d.\n", ret);
		return -EINVAL;
	}

	query_cmd_param->out->data_size = query_cmd_param->out_len;
	scc->data_size = sizeof(u32);

	return ubctl_scc_data_deal(ucdev, pkt_in->index, query_cmd_param->out, scc);
}

static int ubctl_scc_log_deal(struct ubctl_dev *ucdev,
			      struct ubctl_query_cmd_param *query_cmd_param,
			      struct ubctl_cmd *cmd, u32 out_len, u32 offset)
{
	struct fwctl_pkt_in_index *pkt_in = (struct fwctl_pkt_in_index *)query_cmd_param->in->data;
	struct ubctl_scc_data *scc = (struct ubctl_scc_data *)cmd->out_data;
	int ret = 0;

	if (query_cmd_param->in->data_size != sizeof(*pkt_in)) {
		ubctl_err(ucdev, "user data of scc log is invalid.\n");
		return -EINVAL;
	}

	ret = ubctl_scc_data_len_check(ucdev, query_cmd_param->out_len,
				       scc->data_size, UBCTL_SCC_SZ_1M);
	if (ret) {
		ubctl_err(ucdev, "scc log data len check failed, ret = %d.\n", ret);
		return -EINVAL;
	}

	query_cmd_param->out->data_size = query_cmd_param->out_len;

	return ubctl_scc_data_deal(ucdev, pkt_in->index, query_cmd_param->out, scc);
}

static int ubctl_query_scc_data(struct ubctl_dev *ucdev,
				struct ubctl_query_cmd_param *query_cmd_param,
				struct ubctl_func_dispatch *query_func)
{
	struct ubctl_query_dp query_dp[] = {
		{ UBCTL_QUERY_SCC_DFX, UBCTL_SCC_LEN, UBCTL_READ, NULL, 0 },
	};

	return ubctl_query_data(ucdev, query_cmd_param, query_func,
				query_dp, ARRAY_SIZE(query_dp));
}

static int ubctl_query_port_infos(struct ubctl_dev *ucdev,
				  struct ubctl_query_cmd_param *query_cmd_param,
				  struct ubctl_func_dispatch *query_func,
				  u32 port_bitmap)
{
#define UBCTL_U32_BIT_NUM 32U

	struct ubctl_query_dp query_dp[] = {
		{ UBCTL_QUERY_DL_LINK_STATUS_DFX, UBCTL_DL_LINK_STATUS_LEN, UBCTL_READ, NULL, 0 },
	};
	u32 iodie_len = sizeof(struct fwctl_rpc_ub_out) + UBCTL_IO_DIE_INFO_LEN;
	u32 out_data_offset = UBCTL_IO_DIE_INFO_LEN / sizeof(u32);
	struct fwctl_rpc_ub_out *out = query_cmd_param->out;
	u32 out_mem_size = query_cmd_param->out_len;
	u32 *pkt_in = query_cmd_param->in->data;
	u32 data_size = out->data_size;
	int port_num = 0;
	int ret = 0;
	u32 i;

	if (port_bitmap == 0)
		return ret;

	struct fwctl_rpc_ub_out *out_temp __free(kvfree) = kvzalloc(iodie_len, GFP_KERNEL);
	if (!out_temp)
		return -ENOMEM;

	query_cmd_param->out = out_temp;

	for (i = 0; i < UBCTL_U32_BIT_NUM; i++) {
		if (!(port_bitmap & (1UL << i)))
			continue;
		out_temp->data_size = 0;
		*pkt_in = i;
		ret = ubctl_query_data(ucdev, query_cmd_param, query_func,
				       query_dp, ARRAY_SIZE(query_dp));
		if (ret != 0)
			break;

		if ((out_temp->data_size + out_data_offset * sizeof(u32)) > out_mem_size) {
			ubctl_err(ucdev, "port info size = %u, total size = %u, offset size = %lu.\n",
				  out_temp->data_size, out_mem_size,
				  out_data_offset * sizeof(u32));
			ret = -ENOMEM;
			break;
		}

		memcpy(&out->data[out_data_offset], out_temp->data, out_temp->data_size);
		data_size += out_temp->data_size;
		out_data_offset += UBCTL_DL_LINK_STATUS_LEN / sizeof(u32);
		port_num++;
	}

	query_cmd_param->out = out;
	out->data_size = data_size;
	out->data[0] = port_num;

	return ret;
}

static int ubctl_query_iodie_info_data(struct ubctl_dev *ucdev,
				       struct ubctl_query_cmd_param *query_cmd_param,
				       struct ubctl_func_dispatch *query_func)
{
	struct ubctl_query_dp query_dp[] = {
		{ UBCTL_QUERY_PORT_NUM_DFX, UBCTL_IO_DIE_INFO_LEN, UBCTL_READ, NULL, 0 },
	};
	u32 port_bitmap;
	int ret;

	ret = ubctl_query_data(ucdev, query_cmd_param, query_func,
			       query_dp, ARRAY_SIZE(query_dp));
	if (ret != 0)
		return ret;

	if (query_cmd_param->out->data_size < sizeof(u32))
		return -ENOMEM;
	port_bitmap = *query_cmd_param->out->data;

	return ubctl_query_port_infos(ucdev, query_cmd_param, query_func, port_bitmap);
}

static struct ubctl_func_dispatch g_ubctl_query_func[] = {
	{ UTOOL_CMD_QUERY_DL_LINK_TRACE, ubctl_query_dl_trace_data,
	  ubctl_trace_data_deal },

	{ UTOOL_CMD_QUERY_SCC_VERSION, ubctl_query_scc_data, ubctl_scc_version_deal},
	{ UTOOL_CMD_QUERY_SCC_LOG, ubctl_query_scc_data, ubctl_scc_log_deal },

	{ UTOOL_CMD_QUERY_IO_DIE_PORT_INFO, ubctl_query_iodie_info_data,
	  ubctl_query_data_deal },

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

/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright(c) 2025 HiSilicon Technologies CO., Ltd. All rights reserved.
 */

#ifndef __UB_COMMAND_H__
#define __UB_COMMAND_H__

#include <linux/auxiliary_bus.h>
#include <linux/fwctl.h>
#include <linux/kfifo.h>

#include <uapi/fwctl/ub_fwctl.h>

#define UBCTL_READ true
#define UBCTL_WRITE false

#define ubctl_err(ucdev, format, ...) \
	dev_err(&ucdev->fwctl.dev, format, ##__VA_ARGS__)

#define ubctl_dbg(ucdev, format, ...) \
	dev_dbg(&ucdev->fwctl.dev, "PID %u: " format, current->pid, \
		##__VA_ARGS__)

#define ubctl_info(ucdev, format, ...) \
	dev_info(&ucdev->fwctl.dev, "PID %u: " format, current->pid, \
		##__VA_ARGS__)

#define UBCTL_GET_PHY_ADDR(high, low) ((((u64)(high)) << 32) | (low))
#define UBCTL_EXTRACT_BITS(value, start, end) \
	(((value) >> (start)) & ((1UL << ((end) - (start) + 1)) - 1))

struct ubctl_dev {
	struct fwctl_device fwctl;
	DECLARE_KFIFO_PTR(ioctl_fifo, unsigned long);
	struct auxiliary_device *adev;
};

struct ubctl_query_cmd_param {
	size_t in_len;
	struct fwctl_rpc_ub_in *in;
	size_t out_len;
	struct fwctl_rpc_ub_out *out;
};

struct ubctl_cmd {
	u32 op_code;
	u32 is_read;
	u32 in_len;
	u32 out_len;
	void *in_data;
	void *out_data;
};

struct ubctl_func_dispatch {
	u32 rpc_cmd;
	int (*execute)(struct ubctl_dev *ucdev,
		       struct ubctl_query_cmd_param *query_cmd_param,
		       struct ubctl_func_dispatch *query_func);
	int (*data_deal)(struct ubctl_dev *ucdev,
			 struct ubctl_query_cmd_param *query_cmd_param,
			 struct ubctl_cmd *cmd, u32 out_len, u32 offset_index);
};

struct ubctl_query_dp {
	u32 op_code;
	u32 out_len;
	bool is_read;
	void *data;
	u32 data_len;
};

struct ubctl_query_cmd_dp {
	struct ubctl_func_dispatch *query_func;
	void *cmd_in;
	void *cmd_out;
};

int ubctl_ubase_cmd_send(struct auxiliary_device *adev,
			 struct ubctl_cmd *cmd);
int ubctl_fill_cmd(struct ubctl_cmd *cmd, void *cmd_in, void *cmd_out,
		   u32 out_len, u32 is_read);
int ubctl_query_data(struct ubctl_dev *ucdev,
		     struct ubctl_query_cmd_param *query_cmd_param,
		     struct ubctl_func_dispatch *query_func,
		     struct ubctl_query_dp *query_dp, u32 query_dp_num);
int ubctl_query_data_deal(struct ubctl_dev *ucdev,
			  struct ubctl_query_cmd_param *query_cmd_param,
			  struct ubctl_cmd *cmd, u32 out_len, u32 offset);

#endif

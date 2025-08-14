// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright(c) 2025 HiSilicon Technologies CO., Ltd. All rights reserved.
 */

#include <linux/auxiliary_bus.h>
#include <linux/ioport.h>
#include <linux/module.h>
#include <linux/io.h>

#include "ub_common.h"
#include "ub_cmd_reg.h"

#define MAX_IOCTL_COUNT 1024
#define TIME_WINDOW_MS 3000
#define TIME_WINDOW_JIFFIES msecs_to_jiffies(TIME_WINDOW_MS)

struct ubctl_uctx {
	struct fwctl_uctx uctx;
};

static int ubctl_open_uctx(struct fwctl_uctx *uctx)
{
	return 0;
}

static void ubctl_close_uctx(struct fwctl_uctx *uctx)
{

}

static void *ubctl_fw_info(struct fwctl_uctx *uctx, size_t *length)
{
	return NULL;
}

static int ubctl_legitimacy_rpc(struct ubctl_dev *ucdev, size_t out_len,
				enum fwctl_rpc_scope scope)
{
	/*
	 * Verify if RPC (Remote Procedure Call) requests are valid.
	 * It determines whether the request is within the allowed time window
	 * and whether the output length meets the requirements by checking
	 * the timestamp and output length of the request.
	 */
	unsigned long current_jiffies = jiffies;
	unsigned long earliest_jiffies = current_jiffies - TIME_WINDOW_JIFFIES;
	unsigned long record_jiffies = 0;
	int kfifo_ret = 0;

	while (kfifo_peek(&ucdev->ioctl_fifo, &record_jiffies) && record_jiffies) {
		if (time_after(record_jiffies, earliest_jiffies))
			break;

		kfifo_ret = kfifo_get(&ucdev->ioctl_fifo, &record_jiffies);
		if (!kfifo_ret) {
			ubctl_err(ucdev, "unexpected events occurred while obtaining data.\n");
			return kfifo_ret;
		}
	}

	if (kfifo_is_full(&ucdev->ioctl_fifo)) {
		ubctl_err(ucdev, "the current number of valid requests exceeds the limit.\n");
		return -EBADMSG;
	}

	kfifo_ret = kfifo_put(&ucdev->ioctl_fifo, current_jiffies);
	if (!kfifo_ret) {
		ubctl_err(ucdev, "unexpected events occurred while writing data.\n");
		return kfifo_ret;
	}

	if (out_len < sizeof(struct fwctl_rpc_ub_out)) {
		ubctl_dbg(ucdev, "outlen %zu is less than min value %zu.\n",
			  out_len, sizeof(struct fwctl_rpc_ub_out));
		return -EBADMSG;
	}

	if (scope != FWCTL_RPC_CONFIGURATION &&
	    scope != FWCTL_RPC_DEBUG_READ_ONLY)
		return -EOPNOTSUPP;

	return 0;
}

static int ubctl_cmd_err(struct ubctl_dev *ucdev, int ret, struct fwctl_rpc_ub_out *out)
{
	/* Keep rpc_out as contains useful debug info for userspace */
	if (!ret || out->retval)
		return 0;

	return ret;
}

static int ub_cmd_do(struct ubctl_dev *ucdev,
		     struct ubctl_query_cmd_param *query_cmd_param)
{
	u32 rpc_cmd = query_cmd_param->in->rpc_cmd;
	struct ubctl_func_dispatch *ubctl_query_reg = ubctl_get_query_reg_func(
		ucdev, rpc_cmd);
	int ret;

	if (ubctl_query_reg && ubctl_query_reg->execute) {
		ret = ubctl_query_reg->execute(ucdev, query_cmd_param,
					       ubctl_query_reg);
	} else {
		ubctl_err(ucdev, "No corresponding query was found.\n");
		return -EINVAL;
	}

	return ubctl_cmd_err(ucdev, ret, query_cmd_param->out);
}

static void *ubctl_fw_rpc(struct fwctl_uctx *uctx, enum fwctl_rpc_scope scope,
			  void *rpc_in, size_t in_len, size_t *out_len)
{
	struct ubctl_dev *ucdev = container_of(uctx->fwctl, struct ubctl_dev,
					       fwctl);
	u32 opcode = ((struct fwctl_rpc_ub_in *)rpc_in)->rpc_cmd;
	struct ubctl_query_cmd_param query_cmd_param;
	void *rpc_out;
	int ret;

	ubctl_dbg(ucdev, "cmdif: opcode 0x%x inlen %zu outlen %zu\n",
		  opcode, in_len, *out_len);

	ret = ubctl_legitimacy_rpc(ucdev, *out_len, scope);
	if (ret)
		return ERR_PTR(ret);

	rpc_out = kvzalloc(*out_len, GFP_KERNEL);
	if (!rpc_out)
		return ERR_PTR(-ENOMEM);

	query_cmd_param.out = rpc_out;
	query_cmd_param.in = rpc_in;
	query_cmd_param.out_len = *out_len - offsetof(struct fwctl_rpc_ub_out, data);
	query_cmd_param.in_len = in_len;

	ret = ub_cmd_do(ucdev, &query_cmd_param);

	ubctl_dbg(ucdev, "cmdif: opcode 0x%x retval %d\n", opcode, ret);

	return rpc_out;
}

static const struct fwctl_ops ubctl_ops = {
	.device_type = FWCTL_DEVICE_TYPE_UB,
	.uctx_size = sizeof(struct ubctl_uctx),
	.open_uctx = ubctl_open_uctx,
	.close_uctx = ubctl_close_uctx,
	.info = ubctl_fw_info,
	.fw_rpc = ubctl_fw_rpc,
};

DEFINE_FREE(ubctl, struct ubctl_dev *, if (_T) fwctl_put(&_T->fwctl))

static int ubctl_probe(struct auxiliary_device *adev,
		       const struct auxiliary_device_id *id)
{
	struct ubctl_dev *ucdev __free(ubctl) = fwctl_alloc_device(
		adev->dev.parent, &ubctl_ops, struct ubctl_dev, fwctl);
	int ret;

	if (!ucdev)
		return -ENOMEM;

	ret = kfifo_alloc(&ucdev->ioctl_fifo, MAX_IOCTL_COUNT, GFP_KERNEL);
	if (ret) {
		ubctl_err(ucdev, "kfifo alloc device failed, retval = %d.\n", ret);
		return -ENOMEM;
	}

	ret = fwctl_register(&ucdev->fwctl);
	if (ret) {
		ubctl_err(ucdev, "fwctl register failed, retval = %d.\n", ret);
		kfifo_free(&ucdev->ioctl_fifo);
		return ret;
	}

	ucdev->adev = adev;
	auxiliary_set_drvdata(adev, no_free_ptr(ucdev));
	return 0;
}

static void ubctl_remove(struct auxiliary_device *adev)
{
	struct ubctl_dev *ucdev = auxiliary_get_drvdata(adev);

	fwctl_unregister(&ucdev->fwctl);
	kfifo_free(&ucdev->ioctl_fifo);
	fwctl_put(&ucdev->fwctl);
}

static const struct auxiliary_device_id ubctl_id_table[] = {
	{
		.name = "ubase.fwctl",
	},
	{}
};
MODULE_DEVICE_TABLE(auxiliary, ubctl_id_table);

static struct auxiliary_driver ubctl_driver = {
	.name = "ub_fwctl",
	.probe = ubctl_probe,
	.remove = ubctl_remove,
	.id_table = ubctl_id_table,
};

module_auxiliary_driver(ubctl_driver);

MODULE_IMPORT_NS(FWCTL);
MODULE_DESCRIPTION("UB fwctl driver");
MODULE_AUTHOR("HiSilicon Tech. Co., Ltd.");
MODULE_LICENSE("GPL");

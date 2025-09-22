// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) HiSilicon Technologies Co., Ltd. 2025. All rights reserved.
 */

#define pr_fmt(fmt) "ubus hisi ctl: " fmt

#include <linux/debugfs.h>
#include <linux/resource_ext.h>
#include <ub/ubus/ubus.h>

#include "../../ubus_controller.h"
#include "hisi-ubus.h"

static struct ub_bus_controller_ops hi_ubc_ops = {
	.register_decoder_base_addr = hi_register_decoder_base_addr,
};

void hi_register_decoder_base_addr(struct ub_bus_controller *ubc, u64 *cmd_queue,
				   u64 *event_queue)
{
	struct hi_ubc_private_data *data = (struct hi_ubc_private_data *)ubc->data;

	*cmd_queue = data->io_decoder_cmdq;
	*event_queue = data->io_decoder_evtq;
}

static void ub_bus_controller_debugfs_init(struct ub_bus_controller *ubc)
{
	if (!debugfs_initialized())
		return;

	ubc->debug_root = debugfs_create_dir(ubc->name, NULL);
}

static void ub_bus_controller_debugfs_uninit(struct ub_bus_controller *ubc)
{
	debugfs_remove_recursive(ubc->debug_root);
	ubc->debug_root = NULL;
}

int ub_bus_controller_probe(struct ub_bus_controller *ubc)
{
	ubc->ops = &hi_ubc_ops;
	ub_bus_controller_debugfs_init(ubc);

	return 0;
}

void ub_bus_controller_remove(struct ub_bus_controller *ubc)
{
	ub_bus_controller_debugfs_uninit(ubc);
	ubc->ops = NULL;
}

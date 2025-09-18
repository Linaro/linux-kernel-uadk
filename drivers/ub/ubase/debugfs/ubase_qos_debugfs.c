// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved.
 *
 */

#include "ubase_debugfs.h"
#include "ubase_hw.h"
#include "ubase_mailbox.h"
#include "ubase_qos_debugfs.h"

static void ubase_dbg_dump_adev_vl_info(struct seq_file *s,
					struct ubase_adev_qos *qos)
{
	seq_puts(s, "vl:");
	ubase_dbg_dump_arr_info(s, qos->vl, qos->vl_num);

	seq_puts(s, "tp_req_vl:");
	ubase_dbg_dump_arr_info(s, qos->tp_req_vl, qos->tp_vl_num);

	seq_puts(s, "ctp_req_vl:");
	ubase_dbg_dump_arr_info(s, qos->ctp_req_vl, qos->ctp_vl_num);

	seq_puts(s, "nic_vl:");
	ubase_dbg_dump_arr_info(s, qos->nic_vl, qos->nic_vl_num);
}

static void ubase_dbg_dump_adev_sl_info(struct seq_file *s,
					struct ubase_adev_qos *qos)
{
	seq_puts(s, "sl:");
	ubase_dbg_dump_arr_info(s, qos->sl, qos->sl_num);

	seq_puts(s, "tp_sl:");
	ubase_dbg_dump_arr_info(s, qos->tp_sl, qos->tp_sl_num);

	seq_puts(s, "ctp_sl:");
	ubase_dbg_dump_arr_info(s, qos->ctp_sl, qos->ctp_sl_num);

	seq_puts(s, "nic_sl:");
	ubase_dbg_dump_arr_info(s, qos->nic_sl, qos->nic_sl_num);
}

int ubase_dbg_dump_adev_qos_info(struct seq_file *s, void *data)
{
	struct ubase_dev *udev = dev_get_drvdata(s->private);
	struct ubase_adev_qos *qos = &udev->qos;
	struct ubase_dbg_adev_qos_info {
		const char *format;
		u8 qos_info;
	} adev_qos_info[] = {
		{"vl_num: %u\n", qos->vl_num},
		{"tp_vl_num: %u\n", qos->tp_vl_num},
		{"ctp_vl_num: %u\n", qos->ctp_vl_num},
		{"tp_resp_vl_offset: %u\n", qos->tp_resp_vl_offset},
		{"ctp_resp_vl_offset: %u\n", qos->ctp_resp_vl_offset},
		{"sl_num: %u\n", qos->sl_num},
		{"tp_sl_num: %u\n", qos->tp_sl_num},
		{"ctp_sl_num: %u\n", qos->ctp_sl_num},
		{"nic_sl_num: %u\n", qos->nic_sl_num},
		{"nic_vl_num: %u\n", qos->nic_vl_num},
	};
	int i;

	for (i = 0; i < ARRAY_SIZE(adev_qos_info); i++)
		seq_printf(s, adev_qos_info[i].format, adev_qos_info[i].qos_info);

	ubase_dbg_dump_adev_vl_info(s, qos);
	ubase_dbg_dump_adev_sl_info(s, qos);

	return 0;
}

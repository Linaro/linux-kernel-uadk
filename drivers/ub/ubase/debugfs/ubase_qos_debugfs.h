/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved.
 *
 */

#ifndef __UBASE_QOS_DEBUGFS_H__
#define __UBASE_QOS_DEBUGFS_H__

struct device;

int ubase_dbg_dump_sl_vl_map(struct seq_file *s, void *data);
int ubase_dbg_dump_udma_dscp_vl_map(struct seq_file *s, void *data);
int ubase_dbg_dump_ets_tc_info(struct seq_file *s, void *data);
int ubase_dbg_dump_ets_tcg_info(struct seq_file *s, void *data);
int ubase_dbg_dump_ets_port_info(struct seq_file *s, void *data);
int ubase_dbg_dump_rack_vl_bitmap(struct seq_file *s, void *data);
int ubase_dbg_dump_adev_qos_info(struct seq_file *s, void *data);
int ubase_dbg_dump_tm_queue_info(struct seq_file *s, void *data);
int ubase_dbg_dump_tm_qset_info(struct seq_file *s, void *data);
int ubase_dbg_dump_tm_pri_info(struct seq_file *s, void *data);
int ubase_dbg_dump_tm_pg_info(struct seq_file *s, void *data);
int ubase_dbg_dump_tm_port_info(struct seq_file *s, void *data);

#endif

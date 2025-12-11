/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved.
 *
 */

#ifndef _UB_UBASE_COMM_DEBUGFS_H_
#define _UB_UBASE_COMM_DEBUGFS_H_

#include <linux/auxiliary_bus.h>

struct ubase_dbgfs;

struct ubase_dbg_dentry_info {
	const char *name;
	struct dentry *dentry;
	u32 property;
	bool (*support)(struct device *dev, u32 property);
};

struct ubase_dbg_cmd_info {
	const char *name;
	int dentry_index;
	u32 property;
	bool (*support)(struct device *dev, u32 property);
	int (*init)(struct device *dev, struct ubase_dbg_dentry_info *dirs,
		    struct ubase_dbgfs *dbgfs, u32 idx);
	int (*read_func)(struct seq_file *s, void *data);
};

struct ubase_dbgfs {
	struct dentry			*dentry; /* dbgfs root path */
	struct ubase_dbg_cmd_info	*cmd_info;
	int				cmd_info_size;
};

int ubase_dbg_seq_file_init(struct device *dev,
			    struct ubase_dbg_dentry_info *dirs,
			    struct ubase_dbgfs *dbgfs, u32 idx);
int ubase_dbg_create_dentry(struct device *dev, struct ubase_dbgfs *dbgfs,
			    struct ubase_dbg_dentry_info *dirs, u32 root_idx);
struct dentry *ubase_diag_debugfs_root(struct auxiliary_device *adev);
void ubase_print_context_hw(struct seq_file *s, void *ctx_addr, u32 ctx_len);
bool ubase_dbg_dentry_support(struct auxiliary_device *adev, u32 property);
int ubase_dbg_format_time(time64_t time, struct seq_file *s);

#endif

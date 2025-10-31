/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved.
 *
 */

#ifndef __UBASE_DEBUGFS_H__
#define __UBASE_DEBUGFS_H__

#include <linux/debugfs.h>
#include <linux/kernel.h>
#include <ub/ubase/ubase_comm_debugfs.h>

#include "ubase_dev.h"

enum ubase_dbg_dentry_type {
	UBASE_DBG_DENTRY_CONTEXT = 0,
	UBASE_DBG_DENTRY_QOS,
	/* must be the last entry. */
	UBASE_DBG_DENTRY_ROOT,
};

int ubase_dbg_init(struct ubase_dev *udev);
void ubase_dbg_uninit(struct ubase_dev *udev);
int ubase_dbg_register_debugfs(void);
void ubase_dbg_unregister_debugfs(void);

#endif

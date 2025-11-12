/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved.
 *
 */

#ifndef __UNIC_DEBUGFS_H__
#define __UNIC_DEBUGFS_H__

#include <linux/in6.h>
#include <ub/ubase/ubase_comm_debugfs.h>

#define unic_get_ubase_root_dentry(adev) ubase_diag_debugfs_root(adev)

enum unic_dbg_dentry_type {
	/* must be the last entry. */
	UNIC_DBG_DENTRY_ROOT
};

int unic_dbg_init(struct auxiliary_device *adev);
void unic_dbg_uninit(struct auxiliary_device *adev);

#endif /* __UNIC_DEBUGFS_H__ */

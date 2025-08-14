/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) 2025 HiSilicon Technologies Co., Ltd. 2025-2025. All rights reserved.
 */

#ifndef __UB_CMD_REG_H__
#define __UB_CMD_REG_H__

#include "ub_common.h"

struct ubctl_func_dispatch *ubctl_get_query_reg_func(struct ubctl_dev *ucdev,
						     u32 rpc_cmd);
#endif

/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2022-2025. All rights reserved.
 *
 * Description: ubcore tp header
 * Author: Yan Fangfang
 * Create: 2022-09-08
 * Note:
 * History: 2022-09-208: Create file
 */

#ifndef UBCORE_TP_H
#define UBCORE_TP_H

#include <ub/urma/ubcore_types.h>

static inline bool ubcore_have_ops(struct ubcore_device *dev)
{
	return (dev && dev->ops);
}

static inline bool ubcore_have_tp_ops(struct ubcore_device *dev)
{
	return (dev && dev->ops &&
		dev->ops->create_tp && dev->ops->modify_tp);
}

static inline bool ubcore_have_tp_ctrlplane_ops(struct ubcore_device *dev)
{
	return (dev && dev->ops &&
		dev->ops->get_tp_list && dev->ops->active_tp);
}

#endif

/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 *
 * Description: ubcore connect bonding header file
 * Author: Wang Hang
 * Create: 2025-08-07
 * Note:
 * History: 2025-08-07: create file
 */

#ifndef UBCORE_CONNECT_BONDING_H
#define UBCORE_CONNECT_BONDING_H

#include <ub/urma/ubcore_types.h>

#define UBAGG_DEV_PREFIX "bonding_dev"

static inline bool ubcore_is_bonding_dev(struct ubcore_device *dev)
{
	return memcmp(dev->dev_name, UBAGG_DEV_PREFIX,
		      strlen(UBAGG_DEV_PREFIX)) == 0;
}

int ubcore_connect_exchange_udata_when_import_seg(struct ubcore_seg *seg,
						  struct ubcore_udata *udata);

int ubcore_connect_exchange_udata_when_import_jetty(
	struct ubcore_tjetty_cfg *cfg, struct ubcore_udata *udata, bool is_jfr);

void ubcore_connect_bonding_init(void);

#endif

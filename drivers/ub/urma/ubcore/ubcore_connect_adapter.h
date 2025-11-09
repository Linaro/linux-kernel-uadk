/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 *
 * Description: ubcore connect adapter header file
 * Author: Wang Hang
 * Create: 2025-06-19
 * Note:
 * History: 2025-06-19: create file
 */

#ifndef UBCORE_CONNECT_ADAPTER_H
#define UBCORE_CONNECT_ADAPTER_H

#include "ub/urma/ubcore_types.h"

struct ubcore_ex_tp_info {
	struct hlist_node hnode; /* key: tp_handle */
	uint64_t tp_handle;
	struct kref ref_cnt;
};

struct ubcore_tjetty *ubcore_import_jfr_compat(struct ubcore_device *dev,
					       struct ubcore_tjetty_cfg *cfg,
					       struct ubcore_udata *udata);

struct ubcore_tjetty *ubcore_import_jetty_compat(struct ubcore_device *dev,
						 struct ubcore_tjetty_cfg *cfg,
						 struct ubcore_udata *udata);

int ubcore_bind_jetty_compat(struct ubcore_jetty *jetty,
			     struct ubcore_tjetty *tjetty,
			     struct ubcore_udata *udata);

int ubcore_adapter_layer_disconnect(struct ubcore_vtpn *vtpn);

void ubcore_exchange_init(void);

static inline bool ubcore_check_ctrlplane_compat(void *op_ptr)
{
	return (op_ptr == NULL);
}

#endif

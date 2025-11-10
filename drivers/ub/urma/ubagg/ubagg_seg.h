/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 *
 * Description: ubagg kernel module
 * Author: Weicheng Zhang
 * Create: 2025-8-6
 * Note:
 * History: 2025-8-6: Create file
 */

#ifndef UBAGG_SEG_H
#define UBAGG_SEG_H

#include "ubagg_types.h"
#include "ubagg_hash_table.h"

struct ubcore_target_seg *ubagg_register_seg(struct ubcore_device *dev,
					     struct ubcore_seg_cfg *cfg,
					     struct ubcore_udata *udata);

int ubagg_unregister_seg(struct ubcore_target_seg *seg);

struct ubcore_target_seg *ubagg_import_seg(struct ubcore_device *dev,
					   struct ubcore_target_seg_cfg *cfg,
					   struct ubcore_udata *udata);

int ubagg_unimport_seg(struct ubcore_target_seg *tseg);

int ubagg_init_seg_bitmap(void);

int ubagg_init_seg_ht(void);

void ubagg_uninit_seg_bitmap(void);

#endif // UBAGG_SEG_H

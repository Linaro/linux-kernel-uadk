/* SPDX-License-Identifier: GPL-2.0+ */
/* Copyright(c) 2025 HiSilicon Technologies CO., Ltd. All rights reserved. */

#ifndef __UDMA_DB_H__
#define __UDMA_DB_H__

#include "udma_ctx.h"
#include "udma_dev.h"

int udma_pin_sw_db(struct udma_context *ctx, struct udma_sw_db *db);
void udma_unpin_sw_db(struct udma_context *ctx, struct udma_sw_db *db);
int udma_alloc_sw_db(struct udma_dev *dev, struct udma_sw_db *db,
		     enum udma_db_type type);
void udma_free_sw_db(struct udma_dev *dev, struct udma_sw_db *db);

#endif /* __UDMA_DB_H__ */

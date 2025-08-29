/* SPDX-License-Identifier: GPL-2.0+ */
/* Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved. */

#ifndef __CDMA_DB_H__
#define __CDMA_DB_H__

#include "cdma.h"

struct cdma_context;

struct cdma_sw_db_page {
	struct list_head list;
	struct cdma_umem *umem;
	u64 user_virt;
	refcount_t refcount;
};

struct cdma_k_sw_db_page {
	struct list_head list;
	u32 num_db;
	unsigned long *bitmap;
	struct cdma_buf db_buf;
};

struct cdma_sw_db {
	union {
		struct cdma_sw_db_page *page;
		struct cdma_k_sw_db_page *kpage;
	};
	u32 index;
	u64 db_addr;
	u32 *db_record;
};

void cdma_unpin_sw_db(struct cdma_context *ctx, struct cdma_sw_db *db);

void cdma_free_sw_db(struct cdma_dev *dev, struct cdma_sw_db *db);

#endif /* CDMA_DB_H */

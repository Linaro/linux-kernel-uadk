/* SPDX-License-Identifier: GPL-2.0+ */
/* Copyright(c) 2025 HiSilicon Technologies CO., Ltd. All rights reserved. */

#ifndef __UDMA_TID_H__
#define __UDMA_TID_H__

#include "udma_dev.h"

struct udma_tid {
	struct ubcore_token_id core_key_id;
	bool kernel_mode;
	uint32_t tid;
};

static inline struct udma_tid *to_udma_tid(struct ubcore_token_id *token_id)
{
	return container_of(token_id, struct udma_tid, core_key_id);
}

struct ubcore_token_id *udma_alloc_tid(struct ubcore_device *dev,
				       union ubcore_token_id_flag flag,
				       struct ubcore_udata *udata);
int udma_free_tid(struct ubcore_token_id *token_id);

#endif /* __UDMA_TID_H__ */

/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2022-2025. All rights reserved.
 *
 * Description: uburma event header
 * Author: Yan Fangfang
 * Create: 2022-07-28
 * Note:
 * History: 2022-07-28: create file
 */

#ifndef UBURMA_EVENT_H
#define UBURMA_EVENT_H

#include "ub/urma/ubcore_types.h"
#include "uburma_uobj.h"

void uburma_init_jfe(struct uburma_jfe *jfe);
void uburma_uninit_jfe(struct uburma_jfe *jfe);

typedef void (*uburma_jfe_event_data_free_fn)(uint64_t event_data);

void uburma_write_event_with_free_fn(
	struct uburma_jfe *jfe, uint64_t event_data, uint32_t event_type,
	struct list_head *obj_event_list, uint32_t *counter,
	uburma_jfe_event_data_free_fn event_data_free_fn);
void uburma_write_event(struct uburma_jfe *jfe, uint64_t event_data,
			uint32_t event_type, struct list_head *obj_event_list,
			uint32_t *counter);

void uburma_release_comp_event(struct uburma_jfce_uobj *jfce,
			       struct list_head *event_list);
void uburma_release_async_event(struct uburma_file *ufile,
				struct list_head *event_list);
#endif /* UBURMA_EVENT_H */

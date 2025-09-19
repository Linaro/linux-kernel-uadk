/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) HiSilicon Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef __UBUS_ENTITY_H__
#define __UBUS_ENTITY_H__

struct ub_entity *ub_alloc_ent(void);
int ub_setup_ent(struct ub_entity *uent);
void ub_entity_add(struct ub_entity *uent, void *ctx);
void ub_start_ent(struct ub_entity *uent);
void ub_remove_ent(struct ub_entity *uent);
void ub_stop_entities(void);
void ub_remove_entities(void);
void ub_disable_ent(struct ub_entity *uent);

#endif /* __UBUS_ENTITY_H__ */

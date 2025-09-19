/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) HiSilicon Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef __EID_H__
#define __EID_H__

struct ub_entity;

int ub_eid_request(guid_t *id, u32 *eid);
void ub_eid_release(u32 eid);
int ub_eid_alloc(struct ub_entity *uent);
void ub_eid_free(struct ub_entity *uent);

#endif /* __EID_H__ */

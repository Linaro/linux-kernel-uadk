/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) HiSilicon Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef __CAP_H__
#define __CAP_H__

void ub_set_cap_bitmap(struct ub_entity *uent);
u32 ub_find_capability(u32 cap);
void ub_init_capabilities(struct ub_entity *uent);
void ub_uninit_capabilities(struct ub_entity *uent);

#endif /* __CAP_H__ */

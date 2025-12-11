/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) HiSilicon Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef __CNA_H__
#define __CNA_H__

struct ub_entity;
int ub_cna_alloc(struct ub_entity *uent);
void ub_cna_free(struct ub_entity *uent);

#endif /* __CNA_H__ */

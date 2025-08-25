/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved.
 * Description: UMMU MATT interface
 */

#ifndef __UMMU_PAGE_TABLE_H__
#define __UMMU_PAGE_TABLE_H__

#include "ummu.h"

int ummu_domain_collect_pgtable(struct ummu_domain *ummu_domain);

#endif /* __UMMU_PAGE_TABLE_H__ */

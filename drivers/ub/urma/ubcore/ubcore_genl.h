/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2025. All rights reserved.
 *
 * Description: define hash table ops
 * Author: Zhao Yanchao
 * Create: 2024-01-18
 * Note:
 * History: 2024-01-18  Zhao Yanchao  Add base code
 */

#ifndef UBCORE_GENL_H
#define UBCORE_GENL_H

#include <ub/urma/ubcore_types.h>

int ubcore_genl_init(void) __init;
void ubcore_genl_exit(void);

#endif

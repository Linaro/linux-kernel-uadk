/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2025. All rights reserved.
 *
 * Description: ubcore main header
 * Author: Zhao Yusu
 * Create: 2024-02-27
 * Note:
 * History: 2024-02-27: Introduce ubcore version API
 */

#ifndef UBCORE_MAIN_H
#define UBCORE_MAIN_H

#include "ubcore_msg.h"

int ubcore_register_notifiers(void);
void ubcore_unregister_notifiers(void);

#endif

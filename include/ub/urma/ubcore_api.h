/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2022-2025. All rights reserved.
 *
 * Description: API definition provided by ubcore to ubep device driver
 * Author: Qian Guoxin
 * Create: 2022-1-25
 * Note:
 * History: 2022-1-25: Create file
 */

#ifndef UBCORE_API_H
#define UBCORE_API_H

#include "ubcore_types.h"

/**
 * Register a device to ubcore
 * @param[in] dev: the ubcore device;
 * @return: 0 on success, other value on error
 */
int ubcore_register_device(struct ubcore_device *dev);
/**
 * Unregister a device from ubcore
 * @param[in] dev: the ubcore device;
 */
void ubcore_unregister_device(struct ubcore_device *dev);

#endif

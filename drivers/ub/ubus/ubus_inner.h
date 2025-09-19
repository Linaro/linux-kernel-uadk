/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) HiSilicon Technologies Co., Ltd. 2025. All rights reserved.
 */
#ifndef __UBUS_INNER_H__
#define __UBUS_INNER_H__

#include <ub/ubus/ubus.h>

struct ub_bus_controller *ub_ubc_get(struct ub_bus_controller *ubc);
void ub_ubc_put(struct ub_bus_controller *ubc);

#endif /* __UBUS_INNER_H__ */

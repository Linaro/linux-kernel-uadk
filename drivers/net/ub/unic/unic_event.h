/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved.
 *
 */

#ifndef __UNIC_EVENT_H__
#define __UNIC_EVENT_H__

#include <linux/netdevice.h>

int unic_register_event(struct auxiliary_device *adev);
void unic_unregister_event(struct auxiliary_device *adev);

#endif

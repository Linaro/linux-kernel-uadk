/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved.
 *
 */

#ifndef __UNIC_DCBNL_H__
#define __UNIC_DCBNL_H__

#include <linux/netdevice.h>

#ifdef CONFIG_UB_UNIC_DCB
void unic_set_dcbnl_ops(struct net_device *netdev);
#else
static inline void unic_set_dcbnl_ops(struct net_device *netdev) {}
#endif

#endif

/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved.
 *
 */

#ifndef __UBASE_PMEM_H__
#define __UBASE_PMEM_H__

#include "ubase_dev.h"

int ubase_prealloc_mem_init(struct ubase_dev *udev);
void ubase_prealloc_mem_uninit(struct ubase_dev *udev);

#endif /* _UBASE_PMEM_H */

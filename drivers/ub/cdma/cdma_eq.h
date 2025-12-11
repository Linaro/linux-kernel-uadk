/* SPDX-License-Identifier: GPL-2.0+ */
/* Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved. */

#ifndef __CDMA_EQ_H__
#define __CDMA_EQ_H__
#include <linux/auxiliary_bus.h>

struct cdma_ae_operation {
	u32 op_code;
	notifier_fn_t call;
};

int cdma_reg_ae_event(struct auxiliary_device *adev);
void cdma_unreg_ae_event(struct auxiliary_device *adev);
int cdma_reg_ce_event(struct auxiliary_device *adev);
void cdma_unreg_ce_event(struct auxiliary_device *adev);

#endif

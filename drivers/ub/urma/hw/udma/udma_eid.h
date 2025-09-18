/* SPDX-License-Identifier: GPL-2.0+ */
/* Copyright(c) 2025 HiSilicon Technologies CO., Ltd. All rights reserved. */

#ifndef __UDMA_EID_H__
#define __UDMA_EID_H__

#include <ub/urma/ubcore_types.h>
#include "udma_cmd.h"

int udma_add_one_eid(struct udma_dev *udma_dev, struct udma_ctrlq_eid_info *eid_info);
int udma_del_one_eid(struct udma_dev *udma_dev, struct udma_ctrlq_eid_info *eid_info);

#endif /* __UDMA_EID_H__ */

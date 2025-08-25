/* SPDX-License-Identifier: GPL-2.0+ */
/* Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved. */

#ifndef __CDMA_DEV_H__
#define __CDMA_DEV_H__

#include <linux/auxiliary_bus.h>
#include <uapi/ub/cdma/cdma_abi.h>

#define CDMA_CTRLQ_EU_UPDATE 0x2
#define CDMA_UE_MAX_NUM 64

struct cdma_dev;
struct eu_info;
struct dev_eid;

struct cdma_ctrlq_eu_info {
	struct eu_info eu;
	u32 op : 4;
	u32 rsvd : 28;
};

enum cdma_ctrlq_eu_op {
	CDMA_CTRLQ_EU_ADD = 0,
	CDMA_CTRLQ_EU_DEL = 1,
};

struct cdma_dev *cdma_create_dev(struct auxiliary_device *adev);
void cdma_destroy_dev(struct cdma_dev *cdev);
struct xarray *get_cdma_dev_tbl(u32 *devices_num);
bool cdma_find_seid_in_eus(struct eu_info *eus, u8 eu_num, struct dev_eid *eid,
			   struct eu_info *eu_out);
int cdma_register_crq_event(struct auxiliary_device *adev);
void cdma_unregister_crq_event(struct auxiliary_device *adev);
int cdma_create_arm_db_page(struct cdma_dev *cdev);
void cdma_destroy_arm_db_page(struct cdma_dev *cdev);

#endif /* _CDMA_DEV_H_ */

// SPDX-License-Identifier: GPL-2.0+
/* Copyright(c) 2025 HiSilicon Technologies CO., Ltd. All rights reserved. */

#define dev_fmt(fmt) "UDMA: " fmt

#include <linux/kernel.h>
#include <linux/ummu_core.h>
#include <ub/ubase/ubase_comm_cmd.h>
#include "udma_dev.h"
#include "udma_cmd.h"
#include "udma_common.h"
#include <ub/urma/ubcore_types.h>
#include "udma_eid.h"

static void udma_dispatch_eid_event(struct udma_dev *udma_dev,
				    struct udma_ctrlq_eid_info *eid_entry,
				    enum ubcore_mgmt_event_type type)
{
	struct ubcore_mgmt_event event = {};
	struct ubcore_eid_info info = {};

	udma_swap_endian(eid_entry->eid.raw, info.eid.raw, sizeof(union ubcore_eid));
	info.eid_index = eid_entry->eid_idx;

	event.ub_dev = &udma_dev->ub_dev;
	event.element.eid_info = &info;
	event.event_type = type;
	ubcore_dispatch_mgmt_event(&event);
}

int udma_add_one_eid(struct udma_dev *udma_dev, struct udma_ctrlq_eid_info *eid_info)
{
	struct udma_ctrlq_eid_info *eid_entry;
	eid_t ummu_eid = 0;
	guid_t guid = {};
	int ret;

	eid_entry = kzalloc(sizeof(struct udma_ctrlq_eid_info), GFP_KERNEL);
	if (!eid_entry)
		return -ENOMEM;

	memcpy(eid_entry, eid_info, sizeof(struct udma_ctrlq_eid_info));
	ret = xa_err(xa_store(&udma_dev->eid_table, eid_info->eid_idx, eid_entry, GFP_KERNEL));
	if (ret) {
		dev_err(udma_dev->dev,
			"save eid entry failed, ret = %d, eid index = %u.\n",
			ret, eid_info->eid_idx);
		goto store_err;
	}

	if (!udma_dev->is_ue) {
		(void)memcpy(&ummu_eid, eid_info->eid.raw, sizeof(ummu_eid));
		ret = ummu_core_add_eid(&guid, ummu_eid, EID_NONE);
		if (ret) {
			dev_err(udma_dev->dev,
				"set ummu eid entry failed, ret is %d.\n", ret);
			goto err_add_ummu_eid;
		}
	}
	udma_dispatch_eid_event(udma_dev, eid_entry, UBCORE_MGMT_EVENT_EID_ADD);

	return ret;
err_add_ummu_eid:
	xa_erase(&udma_dev->eid_table, eid_info->eid_idx);
store_err:
	kfree(eid_entry);

	return ret;
}

int udma_del_one_eid(struct udma_dev *udma_dev, struct udma_ctrlq_eid_info *eid_info)
{
	struct udma_ctrlq_eid_info *eid_entry;
	uint32_t index = eid_info->eid_idx;
	eid_t ummu_eid = 0;
	guid_t guid = {};

	eid_entry = (struct udma_ctrlq_eid_info *)xa_load(&udma_dev->eid_table, index);
	if (!eid_entry) {
		dev_err(udma_dev->dev, "get eid entry failed, eid index = %u.\n",
			index);
		return -EINVAL;
	}
	if (memcmp(eid_entry->eid.raw, eid_info->eid.raw, sizeof(eid_entry->eid.raw))) {
		dev_err(udma_dev->dev, "eid is not match, index = %u.\n", index);
		return -EINVAL;
	}
	xa_erase(&udma_dev->eid_table, index);

	if (!udma_dev->is_ue) {
		(void)memcpy(&ummu_eid, eid_entry->eid.raw, sizeof(ummu_eid));
		ummu_core_del_eid(&guid, ummu_eid, EID_NONE);
	}
	udma_dispatch_eid_event(udma_dev, eid_entry, UBCORE_MGMT_EVENT_EID_RMV);
	kfree(eid_entry);

	return 0;
}

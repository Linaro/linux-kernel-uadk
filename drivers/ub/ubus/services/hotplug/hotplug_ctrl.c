// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) HiSilicon Technologies Co., Ltd. 2025. All rights reserved.
 */

#include "../../ubus.h"
#include "hotplug.h"

/**
 * arrange of SHP cap
 *
 * +-------------+---------------+---------------+
 * | reg(s) name | start address | end address   |
 * | slice header| 0x00          | 0x00          |
 * | slot number | 0x01          | 0x01          |
 * | slot1 regs  | 0x02          | 0x0F          |
 * | slot2 regs  | 0x12          | 0x1F          |
 * ...
 * | slotN regs  | 0x2 + 10(N-1) | 0xF + 10(N-1) |
 */

/**
 * for any given reg in slot regs of slotN, it's pos can be get by:
 * SHP cap pos + 10(N-1) + reg pos in slot regs
 * the fronter two elements is represented as following UB_SLOT_BASE marco
 */
#define UB_SLOT_BASE(slot) (UB_SLOT_START + (slot)->slot_id * UB_SLOT_POS)

int ub_slot_read_dword(struct ub_slot *slot, u32 pos, u32 *val)
{
	int ret = ub_cfg_read_dword(slot->uent, UB_SLOT_BASE(slot) + pos, val);

	if (unlikely(ret))
		ub_err(slot->uent, "ub slot%u read %#x failed, ret=%d\n",
		       slot->slot_id, pos, ret);

	return ret;
}

static void ubhp_start_slot(struct ub_slot *slot)
{
}

static void ubhp_stop_slot(struct ub_slot *slot)
{
}

void ubhp_start_slots(struct ub_entity *uent)
{
	struct ub_slot *slot;

	list_for_each_entry(slot, &uent->slot_list, node)
		ubhp_start_slot(slot);
}

void ubhp_stop_slots(struct ub_entity *uent)
{
	struct ub_slot *slot;

	list_for_each_entry(slot, &uent->slot_list, node)
		ubhp_stop_slot(slot);
}

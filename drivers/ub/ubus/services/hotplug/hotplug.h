/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) HiSilicon Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef __HOTPLUG_H__
#define __HOTPLUG_H__

/**
 * struct ub_slot - UB hotplug slot
 * @uent: pointer to the ub dev who has this slot
 * @r_uent: pointer to the ub dev who is plugged in this slot
 * @ports: port array slice of uent's port array
 * @kobj: kobject of slot to provide sysfs
 * @node: node of slot in uent slot_list;
 *
 * @slot_id: id of this slot
 * @port_start: start port_idx of ports belong to this slot
 * @port_num: number of ports belong to this slot
 */
struct ub_slot {
	/* slot info */
	struct ub_entity *uent;
	struct ub_entity *r_uent;
	struct ub_port *ports;
	struct kobject kobj;
	struct list_head node;

	/* cap info */
	u8 slot_id;
	u16 port_start;
	u16 port_num;
	u32 slot_cap;
};

#define for_each_slot_port(p, s) \
	for ((p) = (s)->ports; ((p) - (s)->ports) < (s)->port_num; (p)++)

#define BUTTON(slot)		((slot)->slot_cap & UB_SLOT_PPS)
#define WORK_LED(slot)		((slot)->slot_cap & UB_SLOT_WLPS)
#define PWR_LED(slot)		((slot)->slot_cap & UB_SLOT_PLPS)
#define PRESENT(slot)		((slot)->slot_cap & UB_SLOT_PDSS)

enum power_state {
	POWER_OFF,
	POWER_ON
};

enum indicator_state {
	INDICATOR_OFF, /* set indicator off */
	INDICATOR_ON, /* set indicator on */
	INDICATOR_BLINKING, /* set indicator blinking */
	INDICATOR_NOOP /* left indicator unchanged */
};

enum hotplug_event {
	HPE_BUTTON_PRESSED = 2,
	HPE_PRESENCE_DETECT,
	HPE_OTHER
};

static inline void ubhp_put_slot(struct ub_slot *slot)
{
	if (slot)
		kobject_put(&slot->kobj);
}

/* ctrl */
int ub_slot_read_dword(struct ub_slot *slot, u32 pos, u32 *val);
void ubhp_set_indicators(struct ub_slot *slot, u8 power, u8 work);
void ubhp_set_slot_power(struct ub_slot *slot, enum power_state power);
bool ubhp_confirm_event(struct ub_slot *slot, enum hotplug_event event);
bool ubhp_wait_linkup(struct ub_slot *slot);
bool ubhp_card_present(struct ub_slot *slot);
void ubhp_start_slots(struct ub_entity *uent);
void ubhp_stop_slots(struct ub_entity *uent);

#endif /* __HOTPLUG_H__ */

// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) HiSilicon Technologies Co., Ltd. 2025. All rights reserved.
 */

#include "../../ubus.h"
#include "../../msg.h"
#include "../../enum.h"
#include "../../route.h"
#include "../../port.h"
#include "../service.h"
#include "hotplug.h"

#define to_ub_slot(s) container_of(s, struct ub_slot, kobj)

static void ubhp_destory_slot(struct ub_slot *slot)
{
	kfree(slot);
}

static void ubhp_slot_release(struct kobject *kobj)
{
	struct ub_slot *slot = to_ub_slot(kobj);

	ubhp_destory_slot(slot);
}

static const struct kobj_type ub_slot_ktype = {
	.release = ubhp_slot_release,
};

static struct ub_slot *ubhp_create_slot(void)
{
	struct ub_slot *slot;

	slot = kzalloc(sizeof(*slot), GFP_KERNEL);
	if (!slot)
		return NULL;

	slot->ports = NULL;
	INIT_LIST_HEAD(&slot->node);

	return slot;
}

static int ubhp_slot_port_check(struct ub_entity *uent, int idx, u32 val)
{
	struct ub_port *ports, *tmp;
	u16 port_start, port_end;
	u16 port_num;

	port_start = (u16)(val & UB_SLOT_START_PORT);
	port_end = (u16)(val >> SZ_16);

	if (port_start > port_end || port_end >= uent->port_nums) {
		ub_err(uent, "slot%d port range err, start=%u, end=%u, total=%u\n",
		       idx, port_start, port_end, uent->port_nums);
		return -EINVAL;
	}

	port_num = port_end - port_start + 1;
	ports = uent->ports + port_start;
	for (tmp = ports; (tmp - ports) < port_num; tmp++) {
		if (tmp->type == VIRTUAL) {
			ub_err(uent, "slot%d port%u type is virtual\n",
			       idx, tmp->index);
			return -EINVAL;
		}

		if (tmp->slot) {
			ub_err(uent, "slot%d port%u already in slot%u\n",
			       idx, tmp->index, tmp->slot->slot_id);
			return -EINVAL;
		}
	}

	return 0;
}

static int ubhp_setup_slot(struct ub_slot *slot, struct ub_entity *uent, int idx)
{
	u16 port_start, port_end;
	u32 val, cap;
	int ret;

	slot->uent = uent;
	slot->slot_id = idx;

	ret = ub_slot_read_dword(slot, UB_SLOT_PORT, &val);
	if (ret)
		return ret;

	ret = ubhp_slot_port_check(uent, idx, val);
	if (ret)
		return ret;

	ret = ub_slot_read_dword(slot, UB_SLOT_CAP, &cap);
	if (ret)
		return ret;

	port_start = (u16)(val & UB_SLOT_START_PORT);
	port_end = (u16)(val >> SZ_16);

	slot->port_start = port_start;
	slot->port_num = port_end - port_start + 1;
	slot->ports = uent->ports + port_start;
	slot->slot_cap = cap;

	ub_info(uent, "slot%u port[%u:%u] cap[%#x] setup\n",
		slot->slot_id, slot->port_start, port_end, slot->slot_cap);

	return 0;
}

static void ubhp_del_slot(struct ub_slot *slot)
{
	struct ub_port *port;

	list_del(&slot->node);

	for_each_slot_port(port, slot)
		port->slot = NULL;

	kobject_del(&slot->kobj);
}

static int ubhp_add_slot(struct ub_slot *slot)
{
	struct ub_entity *uent = slot->uent;
	struct ub_port *port;
	int ret;

	ret = kobject_init_and_add(&slot->kobj, &ub_slot_ktype, &uent->dev.kobj,
				   "slot%d", slot->slot_id);
	if (ret)
		return ret;

	if (slot->ports->r_uent)
		slot->r_uent = slot->ports->r_uent;

	for_each_slot_port(port, slot)
		port->slot = slot;

	list_add(&slot->node, &uent->slot_list);

	return ret;
}

static int ubhp_probe(struct ub_service_device *sdev)
{
	struct ub_entity *uent = sdev->uent;
	struct ub_slot *slot, *tmp;
	u16 slot_num;
	int i, ret;

	if (sdev->service != UB_SERVICE_HP)
		return -ENODEV;

	ret = ub_cfg_read_word(uent, UB_SLOT_START + UB_SLOT_NUM, &slot_num);
	if (ret)
		return ret;

	if (!slot_num) {
		ub_err(uent, "hotplug probe without slot, ignoring\n");
		return -ENODEV;
	}

	for (i = 0; i < slot_num; i++) {
		slot = ubhp_create_slot();
		if (!slot) {
			ret = -ENOMEM;
			goto free_slots;
		}

		ret = ubhp_setup_slot(slot, uent, i);
		if (ret) {
			ubhp_destory_slot(slot);
			goto free_slots;
		}

		ret = ubhp_add_slot(slot);
		if (ret) {
			ubhp_put_slot(slot);
			goto free_slots;
		}
	}

	ubhp_start_slots(uent);

	return 0;
free_slots:
	list_for_each_entry_safe_reverse(slot, tmp, &uent->slot_list, node) {
		ubhp_del_slot(slot);
		ubhp_put_slot(slot);
	}

	return ret;
}

static void ubhp_remove(struct ub_service_device *sdev)
{
	struct ub_entity *uent = sdev->uent;
	struct ub_slot *slot, *tmp;

	ubhp_stop_slots(uent);

	list_for_each_entry_safe(slot, tmp, &uent->slot_list, node) {
		ubhp_del_slot(slot);
		ubhp_put_slot(slot);
	}
}

static struct ub_service_driver ubhp_service_driver = {
	.name = "ub-hotplug",

	.probe = ubhp_probe,
	.remove = ubhp_remove,

	.service = UB_SERVICE_HP,
};

void ubhp_service_init(void)
{
	ub_service_driver_register(&ubhp_service_driver);
}

void ubhp_service_uninit(void)
{
	ub_service_driver_unregister(&ubhp_service_driver);
}

static void ubhp_disconnect_slot(struct ub_slot *slot)
{
	struct ub_port *port;

	for_each_slot_port(port, slot)
		ub_port_disconnect(port);

	slot->r_uent = NULL;
}

/**
 * a simple example for link down
 * for a given topo like
 * +-------------+         +---------+                 +---------+         +--------+
 * | controller0 |p0:---:p0| switch0 |p1:---slot0---:p0| switch1 |p1:---:p0| device0|
 * +-------------+         +---------+                 +---------+         +--------+
 * when slot0 is calling handle link down
 * 1. disconnect slot0 so that p1 and p0 is not connected in software
 * 2. put switch1 and device0 into dev_list, mark them as detached
 * 3. stop switch1 and device0
 * 4. remove switch1 and device0
 * 5. handle route link down at slot0, del route of right(switch1 & device0)
 *	from left(controller0 & switch0)
 */
static void ubhp_handle_link_down(struct ub_slot *slot)
{
	struct list_head dev_list;
	struct ub_entity *r_uent;

	INIT_LIST_HEAD(&dev_list);

	r_uent = slot->r_uent;
	if (!r_uent) {
		ub_warn(slot->uent, "link down without remote dev\n");
		return;
	}

	ubhp_disconnect_slot(slot);
	ubhp_mark_detached_entities(r_uent, &dev_list);
	ubhp_stop_entities(&dev_list);
	ubhp_remove_entities(&dev_list);
	ubhp_update_route_link_down(slot);
}

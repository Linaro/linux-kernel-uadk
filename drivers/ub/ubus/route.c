// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) HiSilicon Technologies Co., Ltd. 2025. All rights reserved.
 */

#define pr_fmt(fmt)	"ubus route: " fmt

#include "ubus.h"
#include "port.h"

#define UB_ROUTE_TABLE_ENTRY_START (UB_ROUTE_TABLE_SLICE_START + (0x10 << 2))
#define EBW(port_nums) ((((port_nums) - 1) >> 5) + 1) /* Entry Bit Width */
#define UB_ROUTE_TABLE_ENTRY_BITS SZ_128

/* node to update routing table */
struct cna_node {
	u32 cna;
	u16 distance; /* distance of path */
	u16 count; /* number of ports that have route to this cna */
	bool need_update; /* need to write to routing table */

	struct kref ref;
	struct list_head node;
};

static struct cna_node *ub_create_cna_node(u32 cna, short distance)
{
	struct cna_node *new_cna;

	new_cna = kzalloc(sizeof(*new_cna), GFP_KERNEL);
	if (!new_cna)
		return NULL;

	new_cna->cna = cna;
	new_cna->need_update = true;
	new_cna->distance = distance;
	new_cna->count = 1;
	kref_init(&new_cna->ref);
	INIT_LIST_HEAD(&new_cna->node);

	return new_cna;
}

static void ub_cna_node_release(struct kref *kref)
{
	struct cna_node *cna = container_of(kref, struct cna_node, ref);

	kfree(cna);
}

static void ub_cna_node_put(struct cna_node *cna)
{
	kref_put(&cna->ref, ub_cna_node_release);
}

static int ub_add_cna_node(u32 cna, short distance, struct list_head *cna_list)
{
	struct list_head *prev_head = cna_list;
	struct cna_node *new_cna, *prev_cna;

	/**
	 * keep the cna list in ascending order to get the smallest cna quickly
	 * traverse from tail to end, insert new node when prev is smaller
	 */
	list_for_each_entry_reverse(prev_cna, cna_list, node) {
		if (prev_cna->cna > cna)
			continue;

		if (prev_cna->cna == cna) {
			prev_cna->count++;
			return 0;
		}

		prev_head = &prev_cna->node;
		break;
	}

	new_cna = ub_create_cna_node(cna, distance);
	if (!new_cna)
		return -ENOMEM;

	list_add(&new_cna->node, prev_head);
	return 0;
}

static void ub_clear_cna_list(struct list_head *cna_list)
{
	struct cna_node *cna, *tmp;

	list_for_each_entry_safe(cna, tmp, cna_list, node) {
		list_del(&cna->node);
		ub_cna_node_put(cna);
	}
}

void ub_route_clear(struct ub_entity *uent)
{
	struct ub_port *port;

	if (!uent)
		return;

	for_each_uent_port(port, uent)
		bitmap_clear(port->cna_maps, 0, UB_ROUTE_TABLE_ENTRY_BITS);

	ub_clear_cna_list(&uent->cna_list);
}

int ub_route_add_entry(struct ub_port *port, u32 cna, short distance)
{
	if (!port)
		return -EINVAL;

	if (test_bit(cna, port->cna_maps))
		return -EEXIST;

	set_bit(cna, port->cna_maps);
	return ub_add_cna_node(cna, distance, &port->uent->cna_list);
}

static void ub_set_route_table_entry(struct ub_entity *uent, u32 dst_cna,
				     u32 *route_table_entry)
{
	int i;
	u32 offset;

	/* Routing Table Block is not required for single-port devices. */
	if (uent->port_nums == 1)
		return;

	pr_info("cna %#x uent set dstcna %#x route\n", uent->cna, dst_cna);

	for (i = 0; i < EBW(uent->port_nums); i++) {
		offset = ((dst_cna + 1) * EBW(uent->port_nums) + i) << SZ_2;
		ub_cfg_write_dword(uent, UB_ROUTE_TABLE_ENTRY_START + offset,
				   route_table_entry[i]);
	}
}

/**
 * after updating routing table in software, this function must be called
 * to sync the software routing table to config space
 */
void ub_route_sync_dev(struct ub_entity *uent)
{
	DECLARE_BITMAP(route_table_entry, UB_ROUTE_TABLE_ENTRY_BITS);
	struct cna_node *cna_node, *tmp;
	struct ub_port *port;
	u32 cna;

	if (!uent || !ub_entity_test_priv_flag(uent, UB_ENTITY_ROUTE_UPDATED))
		return;

	list_for_each_entry_safe(cna_node, tmp, &uent->cna_list, node) {
		if (!cna_node->need_update)
			continue;

		cna = cna_node->cna;
		bitmap_zero(route_table_entry, UB_ROUTE_TABLE_ENTRY_BITS);

		if (cna_node->count == 0) {
			list_del(&cna_node->node);
			kfree(cna_node);
			goto set_entry;
		}

		for_each_uent_port(port, uent)
			if (test_bit(cna, port->cna_maps))
				set_bit(port->index, route_table_entry);

		cna_node->need_update = false;
set_entry:
		ub_set_route_table_entry(uent, cna, (u32 *)route_table_entry);
	}

	ub_entity_assign_priv_flag(uent, UB_ENTITY_ROUTE_UPDATED, false);
}

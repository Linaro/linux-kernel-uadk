// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) HiSilicon Technologies Co., Ltd. 2025. All rights reserved.
 * Descriptor: Maintain ubrt information acquisition path
 */
#include <linux/fwnode.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <ub/ubfi/ubfi.h>

static LIST_HEAD(ubrt_fwnode_list);
static DEFINE_SPINLOCK(ubrt_fwnode_lock);

int ubrt_fwnode_set(u32 index, enum ubrt_node_type type, struct fwnode_handle *fwnode)
{
	struct ubrt_fwnode *curr;

	if (!fwnode)
		return -ENODEV;

	guard(spinlock)(&ubrt_fwnode_lock);
	list_for_each_entry(curr, &ubrt_fwnode_list, list) {
		if (curr->type == type && curr->index == index) {
			curr->fwnode = fwnode;
			return 0;
		}
	}

	return -ENXIO;
}
EXPORT_SYMBOL_GPL(ubrt_fwnode_set);

struct ubrt_fwnode *ubrt_fwnode_get_by_idx(u32 index, enum ubrt_node_type type)
{
	struct ubrt_fwnode *curr;

	guard(spinlock)(&ubrt_fwnode_lock);
	list_for_each_entry(curr, &ubrt_fwnode_list, list) {
		if (curr->type == type && curr->index == index)
			return curr;
	}
	return NULL;
}
EXPORT_SYMBOL_GPL(ubrt_fwnode_get_by_idx);

struct ubrt_fwnode *ubrt_fwnode_get(struct fwnode_handle *fwnode)
{
	struct ubrt_fwnode *curr;

	guard(spinlock)(&ubrt_fwnode_lock);
	list_for_each_entry(curr, &ubrt_fwnode_list, list) {
		if (curr->fwnode == fwnode)
			return curr;

	}
	return NULL;
}
EXPORT_SYMBOL_GPL(ubrt_fwnode_get);

u32 ubrt_fwnode_get_count(enum ubrt_node_type type)
{
	struct ubrt_fwnode *curr;
	u32 count = 0;

	guard(spinlock)(&ubrt_fwnode_lock);
	list_for_each_entry(curr, &ubrt_fwnode_list, list) {
		if (curr->type == type)
			count++;
	}
	return count;
}
EXPORT_SYMBOL_GPL(ubrt_fwnode_get_count);

int ubrt_fwnode_add(void *node, u32 index, int size, enum ubrt_node_type type)
{
	struct ubrt_fwnode *np;
	void *tmp;

	if (!node)
		return -EINVAL;

	np = kzalloc(sizeof(*np), GFP_KERNEL);
	if (!np)
		return -ENOMEM;

	tmp = kmemdup(node, size, GFP_KERNEL);
	if (!tmp) {
		kfree(np);
		return -ENOMEM;
	}

	INIT_LIST_HEAD(&np->list);
	np->ubrt_node = tmp;
	np->index = index;
	np->type = type;
	guard(spinlock)(&ubrt_fwnode_lock);
	list_add_tail(&np->list, &ubrt_fwnode_list);

	return 0;
}
EXPORT_SYMBOL_GPL(ubrt_fwnode_add);

void ubrt_fwnode_del(u32 index, enum ubrt_node_type type)
{
	struct ubrt_fwnode *curr, *tmp;

	guard(spinlock)(&ubrt_fwnode_lock);
	list_for_each_entry_safe(curr, tmp, &ubrt_fwnode_list, list) {
		if (curr->type == type && curr->index == index) {
			kfree(curr->ubrt_node);
			list_del(&curr->list);
			kfree(curr);
			break;
		}
	}
}
EXPORT_SYMBOL_GPL(ubrt_fwnode_del);

void ubrt_fwnode_del_all(void)
{
	struct ubrt_fwnode *cur_fwnode, *tmp_fwnode;

	guard(spinlock)(&ubrt_fwnode_lock);
	list_for_each_entry_safe(cur_fwnode, tmp_fwnode, &ubrt_fwnode_list, list) {
		kfree(cur_fwnode->ubrt_node);
		list_del(&cur_fwnode->list);
		kfree(cur_fwnode);
	}
}
EXPORT_SYMBOL_GPL(ubrt_fwnode_del_all);

int ubrt_get_interrupt_id(u16 ummu_map, u32 *intr_id)
{
	struct ubrt_fwnode *curr;
	struct ummu_node *node;

	guard(spinlock)(&ubrt_fwnode_lock);
	list_for_each_entry(curr, &ubrt_fwnode_list, list) {
		if (curr->type != UBRT_UMMU || curr->index != ummu_map)
			continue;

		node = (struct ummu_node *)curr->ubrt_node;
		*intr_id = node->intr_id;
		return 0;
	}
	return -ENXIO;
}
EXPORT_SYMBOL_GPL(ubrt_get_interrupt_id);

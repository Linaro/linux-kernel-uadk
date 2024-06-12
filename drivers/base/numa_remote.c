// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2024  Huawei Technologies Co., Ltd.
 * Author: Liu Shixin <liushixin2@huawei.com>
 */

#define pr_fmt(fmt) "NUMA remote: " fmt

#include <linux/device.h>
#include <linux/numa_remote.h>

/* The default distance between local node and remote node */
#define REMOTE_TO_LOCAL_DISTANCE	100
/* The default distance between two remtoe node */
#define REMOTE_TO_REMOTE_DISTANCE	254

static bool numa_remote_enabled __ro_after_init;
static nodemask_t numa_nodes_remote;

bool numa_is_remote_node(int nid)
{
	return !!node_isset(nid, numa_nodes_remote);
}
EXPORT_SYMBOL_GPL(numa_is_remote_node);

static void numa_remote_reset_distance(int nid)
{
	int i;

	for (i = 0; i < MAX_NUMNODES; i++) {
		if (i == nid)
			continue;
		if (!numa_is_remote_node(i)) {
			numa_set_distance(i, nid, REMOTE_TO_LOCAL_DISTANCE);
			numa_set_distance(nid, i, REMOTE_TO_LOCAL_DISTANCE);
		} else {
			numa_set_distance(i, nid, REMOTE_TO_REMOTE_DISTANCE);
			numa_set_distance(nid, i, REMOTE_TO_REMOTE_DISTANCE);
		}
	}
}

void __init numa_register_remote_nodes(void)
{
	int i;

	if (!numa_remote_enabled)
		return;

	for (i = 0; i < MAX_NUMNODES; i++) {
		if (!node_test_and_set(i, numa_nodes_parsed))
			node_set(i, numa_nodes_remote);
	}

	for (i = 0; i < MAX_NUMNODES; i++) {
		if (numa_is_remote_node(i))
			numa_remote_reset_distance(i);
	}

	pr_info("%d nodes", nodes_weight(numa_nodes_remote));
}

static int __init numa_parse_remote_nodes(char *buf)
{
	numa_remote_enabled = true;

	return 0;
}
early_param("numa_remote", numa_parse_remote_nodes);

static int find_unused_remote_node(void)
{
	int nid;

	for_each_node_mask(nid, numa_nodes_remote) {
		if (!node_online(nid))
			return nid;
	}

	return NUMA_NO_NODE;
}

/*
 * Add remote memory to the system as system RAM from CXL or UB.
 * The resource_name (visible via /proc/iomem) has to have the format
 * "System RAM (Remote)".
 *
 * @nid:	which node to online
 * @start:	start address of memory range
 * @size:	size of memory range
 * @flags:	memory hotplug flags
 *
 * Returns:
 *	node in case add memory succeed.
 *	NUMA_NO_NODE in case add memory failed.
 */
int add_memory_remote(int nid, u64 start, u64 size, int flags)
{
	int real_nid = NUMA_NO_NODE;

	if (!numa_remote_enabled)
		return NUMA_NO_NODE;

	if (nid < NUMA_NO_NODE || nid >= MAX_NUMNODES)
		return NUMA_NO_NODE;

	if (nid != NUMA_NO_NODE && !numa_is_remote_node(nid))
		return NUMA_NO_NODE;

	lock_device_hotplug();

	real_nid = (nid == NUMA_NO_NODE) ? find_unused_remote_node() : nid;
	if (real_nid == NUMA_NO_NODE)
		goto unlock;

	if (__add_memory(real_nid, start, size, MHP_MERGE_RESOURCE))
		real_nid = NUMA_NO_NODE;

unlock:
	unlock_device_hotplug();

	return real_nid;
}
EXPORT_SYMBOL_GPL(add_memory_remote);

/*
 * Remove remote memory.
 *
 * Returns:
 *	0 in case of memory hotremove succeed.
 *	-errno in case of memory hotremove failed.
 */
int remove_memory_remote(int nid, u64 start, u64 size)
{
	int ret = -EINVAL;

	if (!numa_remote_enabled)
		return -EINVAL;

	if (nid <= NUMA_NO_NODE || nid >= MAX_NUMNODES)
		return -EINVAL;

	if (!numa_is_remote_node(nid) || !node_online(nid))
		return -EINVAL;

	ret = offline_and_remove_memory(start, size);
	if (ret)
		goto out;

	if (!node_online(nid))
		numa_remote_reset_distance(nid);

out:
	return ret;
}
EXPORT_SYMBOL_GPL(remove_memory_remote);

int numa_remote_set_distance(int target, int *node_ids, int *node_distances,
			     int count)
{
	int i;

	if (!numa_remote_enabled)
		return -EINVAL;

	if (target <= NUMA_NO_NODE || target >= MAX_NUMNODES)
		return -EINVAL;

	if (!numa_is_remote_node(target))
		return -EINVAL;

	for (i = 0; i < count; i++) {
		if (numa_is_remote_node(node_ids[i]))
			return -EINVAL;
	}

	for (i = 0; i < count; i++) {
		numa_set_distance(target, node_ids[i], node_distances[i]);
		numa_set_distance(node_ids[i], target, node_distances[i]);
	}

	return 0;
}
EXPORT_SYMBOL_GPL(numa_remote_set_distance);

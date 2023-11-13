// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2024  Huawei Technologies Co., Ltd.
 * Author: Liu Shixin <liushixin2@huawei.com>
 */

#define pr_fmt(fmt) "NUMA remote: " fmt

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

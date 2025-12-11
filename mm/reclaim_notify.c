// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2025  Huawei Technologies Co., Ltd.
 * Author: Tong Tiangen <tongtiangen@huawei.com>
 */

#include <linux/mm.h>
#include <linux/numa_remote.h>
#include "internal.h"

static BLOCKING_NOTIFIER_HEAD(reclaim_notify_list);

int register_reclaim_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&reclaim_notify_list, nb);
}
EXPORT_SYMBOL_GPL(register_reclaim_notifier);

int unregister_reclaim_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(&reclaim_notify_list, nb);
}
EXPORT_SYMBOL_GPL(unregister_reclaim_notifier);

 /*
  * do_reclaim_notify - notify memory reclaim event
  * @ac: alloc context, for direct reclaim
  * @pgdat: reclaim node, for kswapd
  * @reason: reclaim reason
  */
unsigned long do_reclaim_notify(enum reclaim_reason reason,
				const void *reclaim_context)
{
	struct reclaim_notify_data data;
	int nid, idx = 0;

	if (!numa_remote_enabled)
		return 0;

	if (reason >= RR_TYPES)
		return 0;

	if (WARN_ON(reclaim_context == NULL))
		return 0;

	memset(&data, 0, sizeof(struct reclaim_notify_data));

	if (reason == RR_DIRECT_RECLAIM) {
		struct alloc_context *ac =
			(struct alloc_context *)reclaim_context;
		struct zone *zone;
		struct zoneref *z;
		int last_nid = NUMA_NO_NODE;

		z = ac->preferred_zoneref;
		for_next_zone_zonelist_nodemask(zone, z, ac->highest_zoneidx,
						ac->nodemask) {
			nid = zone_to_nid(zone);
			if (numa_is_remote_node(nid) || nid == last_nid)
				continue;

			last_nid = nid;
			data.nid[idx++] = nid;
			if (idx == MAX_NUMNODES)
				break;
		}

		if (idx == 0)
			return 0;

		data.nr_nid = idx;
		data.sync = true;
	} else {
		pg_data_t *pgdat =  (pg_data_t *)reclaim_context;

		if (numa_is_remote_node(pgdat->node_id))
			return 0;

		data.nid[idx] = pgdat->node_id;
		data.nr_nid = 1;
		data.sync = false;
	}
	data.reason = reason;

	blocking_notifier_call_chain(&reclaim_notify_list, 0, &data);

	return data.nr_freed;
}

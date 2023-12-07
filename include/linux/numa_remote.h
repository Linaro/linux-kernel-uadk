/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2024  Huawei Technologies Co., Ltd.
 * Author: Liu Shixin <liushixin2@huawei.com>
 */
#ifndef _LINUX_REMOTE_MEMORY_H_
#define _LINUX_REMOTE_MEMORY_H_

#include <linux/mm.h>

#ifdef CONFIG_NUMA_REMOTE
bool numa_is_remote_node(int nid);
bool numa_remote_nofallback(int nid);
void numa_register_remote_nodes(void);
int add_memory_remote(int nid, u64 start, u64 size, int flags);
int remove_memory_remote(int nid, u64 start, u64 size);
int numa_remote_set_distance(int target, int *node_ids, int *node_distances,
			     int count);
#else
static inline bool numa_is_remote_node(int nid)
{
	return false;
}

static inline bool numa_remote_nofallback(int nid)
{
	return false;
}

static inline void numa_register_remote_nodes(void)
{
}

static inline int add_memory_remote(int nid, u64 start, u64 size, int flags)
{
	return NUMA_NO_NODE;
}

static inline int remove_memory_remote(int nid, u64 start, u64 size)
{
	return -EINVAL;
}

static inline int numa_remote_set_distance(int target, int *node_ids,
					   int *node_distances, int count)
{
	return -EINVAL;
}
#endif
#endif /* _LINUX_REMOTE_MEMORY_H_ */

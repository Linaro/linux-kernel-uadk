/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2024  Huawei Technologies Co., Ltd.
 * Author: Liu Shixin <liushixin2@huawei.com>
 */
#ifndef _LINUX_REMOTE_MEMORY_H_
#define _LINUX_REMOTE_MEMORY_H_

#include <linux/mm.h>
#include <linux/node.h>

#define MEMORY_KEEP_ISOLATED	1
#define MEMORY_DIRECT_ONLINE 2

#ifdef CONFIG_NUMA_REMOTE
bool numa_is_remote_node(int nid);
bool numa_remote_nofallback(int nid);
bool numa_remote_preonline(int nid);
bool numa_remote_hugetlb_nowatermark(int nid);
void numa_register_remote_nodes(void);
bool numa_remote_try_wait_undo_fake_online(int nid);
int add_memory_remote(int nid, u64 start, u64 size, int flags);
int remove_memory_remote(int nid, u64 start, u64 size);
int numa_remote_set_distance(int target, int *node_ids, int *node_distances,
			     int count);
void numa_remote_register_node(struct node *node);
void numa_remote_unregister_node(struct node *node);
#else
static inline bool numa_is_remote_node(int nid)
{
	return false;
}

static inline bool numa_remote_nofallback(int nid)
{
	return false;
}

static inline bool numa_remote_preonline(int nid)
{
	return false;
}

static inline bool numa_remote_hugetlb_nowatermark(int nid)
{
	return false;
}

static inline void numa_register_remote_nodes(void)
{
}

static inline bool numa_remote_try_wait_undo_fake_online(int nid)
{
	return false;
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

static inline void numa_remote_register_node(struct node *node)
{
}

static inline void numa_remote_unregister_node(struct node *node)
{
}
#endif
#endif /* _LINUX_REMOTE_MEMORY_H_ */

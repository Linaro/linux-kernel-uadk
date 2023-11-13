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
void numa_register_remote_nodes(void);
#else
static inline bool numa_is_remote_node(int nid)
{
	return false;
}

static inline void numa_register_remote_nodes(void)
{
}
#endif
#endif /* _LINUX_REMOTE_MEMORY_H_ */

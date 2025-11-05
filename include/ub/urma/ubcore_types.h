/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2021-2025. All rights reserved.
 *
 * Description: Types definition provided by ubcore to client and ubep device
 * Author: Qian Guoxin, Ouyang Changchun
 * Create: 2021-8-3
 * Note:
 * History: 2021-8-3: Create file
 * History: 2021-11-23: Add segment and jetty management
 */

#ifndef UBCORE_TYPES_H
#define UBCORE_TYPES_H

#include <net/net_namespace.h>
#include <linux/list.h>
#include <linux/device.h>
#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <linux/rcupdate.h>
#include <linux/scatterlist.h>
#include <linux/libfdt_env.h>
#include <linux/mm.h>
#include <linux/netdevice.h>
#include <linux/uuid.h>

#ifdef CONFIG_CGROUP_RDMA
#include <linux/cgroup_rdma.h>
#endif

#include "ubcore_opcode.h"

#define UBCORE_EID_SIZE (16)
#define UBCORE_EID_STR_LEN (39)
#define UBCORE_EID_GROUP_NAME_LEN 10
#define EID_FMT \
	"%2.2x%2.2x:%2.2x%2.2x:%2.2x%2.2x:%2.2x%2.2x:%2.2x%2.2x:%2.2x%2.2x:%2.2x%2.2x:%2.2x%2.2x"
#define EID_UNPACK(...) __VA_ARGS__
#define EID_RAW_ARGS(eid)                                                      \
	EID_UNPACK(eid[0], eid[1], eid[2], eid[3], eid[4], eid[5], eid[6],     \
		   eid[7], eid[8], eid[9], eid[10], eid[11], eid[12], eid[13], \
		   eid[14], eid[15])
#define EID_ARGS(eid) EID_RAW_ARGS((eid).raw)


union ubcore_eid {
	uint8_t raw[UBCORE_EID_SIZE];
	struct {
		uint64_t reserved;
		uint32_t prefix;
		uint32_t addr;
	} in4;
	struct {
		uint64_t subnet_prefix;
		uint64_t interface_id;
	} in6;
};

struct ubcore_eid_info {
	union ubcore_eid eid;
	uint32_t eid_index; /* 0~MAX_EID_CNT -1 */
};

struct ubcore_ueid_cfg {
	union ubcore_eid eid;
	uint32_t upi;
	uint32_t eid_index;
	guid_t guid;
};

struct ubcore_jetty_id {
	union ubcore_eid eid;
	uint32_t id;
};

struct ubcore_ht_param {
	uint32_t size;
	uint32_t node_offset; /* offset of hlist node in the hash table object */
	uint32_t key_offset;
	uint32_t key_size;
	int (*cmp_f)(void *obj, const void *key);
	void (*free_f)(void *obj);
	void (*get_f)(void *obj);
};

struct ubcore_hash_table {
	struct ubcore_ht_param p;
	struct hlist_head *head;
	/* Prevent the same jetty
	 * from being bound by different tjetty
	 */
	struct ubcore_jetty_id rc_tjetty_id;
	spinlock_t lock;
	struct kref kref;
};

enum ubcore_tp_type { UBCORE_RTP, UBCORE_CTP, UBCORE_UTP };

enum ubcore_hash_table_type {
	UBCORE_HT_JFS = 0, /* jfs hash table */
	UBCORE_HT_JFR, /* jfr hash table */
	UBCORE_HT_JFC, /* jfc hash table */
	UBCORE_HT_JETTY, /* jetty hash table */
	UBCORE_HT_TP, /* tp table */
	UBCORE_HT_TPG, /* tpg table */
	UBCORE_HT_RM_VTP, /* rm vtp table */
	UBCORE_HT_RC_VTP, /* rc vtp table */
	UBCORE_HT_UM_VTP, /* um vtp table */
	UBCORE_HT_RM_VTPN, /* rm vtpn table */
	UBCORE_HT_RC_VTPN, /* rc vtpn table */
	UBCORE_HT_UM_VTPN, /* um vtpn table */
	UBCORE_HT_CP_VTPN, /* vtpn table for control plane */
	UBCORE_HT_UTP, /* utp table */
	UBCORE_HT_VTPN, /* vtpn table */
	UBCORE_HT_CTP, /* ctp table */
	UBCORE_HT_EX_TP, /* exchange tp info for control plane */
	UBCORE_HT_RM_CTP_ID, /* key: seid + deid + tag */
	UBCORE_HT_RC_CTP_ID, /* seid + deid + sjettyid + djettyid + tag */
	UBCORE_HT_RM_TP_ID, /* key: seid + deid + tag */
	UBCORE_HT_RC_TP_ID, /* seid + deid + sjettyid + djettyid + tag */
	UBCORE_HT_UTP_ID, /* key: seid + deid + tag */
	UBCORE_HT_NUM
};

union ubcore_umem_flag {
	struct {
		uint32_t non_pin : 1; /* 0: pinned to physical memory. 1: non pin. */
		uint32_t writable : 1; /* 0: read-only. 1: writable. */
		uint32_t reserved : 30;
	} bs;
	uint32_t value;
};

struct ubcore_umem {
	struct ubcore_device *ub_dev;
	struct mm_struct *owning_mm;
	uint64_t length;
	uint64_t va;
	union ubcore_umem_flag flag;
	struct sg_table sg_head;
	uint32_t nmap;
};

enum ubcore_net_addr_op {
	UBCORE_ADD_NET_ADDR = 0,
	UBCORE_DEL_NET_ADDR = 1,
	UBCORE_UPDATE_NET_ADDR = 2
};

enum ubcore_mgmt_event_type {
	UBCORE_MGMT_EVENT_EID_ADD,
	UBCORE_MGMT_EVENT_EID_RMV,
};

struct ubcore_mgmt_event {
	struct ubcore_device *ub_dev;
	union {
		struct ubcore_eid_info *eid_info;
	} element;
	enum ubcore_mgmt_event_type event_type;
};

#endif

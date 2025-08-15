/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved.
 * Description: UMMU Device's implementations
 */

#ifndef __UMMU_H__
#define __UMMU_H__

#include <linux/bitfield.h>
#include <linux/ummu_core.h>
#include <linux/kernel.h>
#include <linux/sizes.h>
#include <linux/init.h>

extern struct platform_driver ummu_driver;

struct ummu_ll_queue {
	union {
		u64 val;
		struct {
			u32 prod;
			u32 cons;
		};
		struct {
			atomic_t prod;
			atomic_t cons;
		} atomic;
		u8 __pad[SMP_CACHE_BYTES];
	} ____cacheline_aligned_in_smp;
	u32 log2size;
};

struct ummu_queue {
	struct ummu_ll_queue llq;
	__le64 *base;
	phys_addr_t base_pa;
	u64 q_base;

	size_t ent_dwords;
	u32 __iomem *prod_reg;
	u32 __iomem *cons_reg;
};

struct ummu_mcmdq {
	struct ummu_queue q;
	atomic_long_t *valid_map;
	atomic_t owner_prod;
	u32 mcmdq_prod;
	rwlock_t mcmdq_lock;
	atomic_t lock;
	int configured;
	int shared;
	void __iomem *base;
};

struct ummu_evtq {
	u32 max_stalls;
};

struct ummu_capability {
#define UMMU_FEAT_2_LVL_TECT		BIT(0)
#define UMMU_FEAT_2_LVL_TCT		BIT(1)
#define UMMU_FEAT_MCMDQ			BIT(2)
#define UMMU_FEAT_EVENTQ		BIT(3)
#define UMMU_FEAT_SEV			BIT(4)
#define UMMU_FEAT_TRANS_S1		BIT(5)
#define UMMU_FEAT_TRANS_S2		BIT(6)
#define UMMU_FEAT_RANGE_INV		BIT(7)
#define UMMU_FEAT_STALLS		BIT(8)
#define UMMU_FEAT_STALL_FORCE		BIT(9)
#define UMMU_FEAT_MSI			BIT(10)
#define UMMU_FEAT_HYP			BIT(11)
#define UMMU_FEAT_HA			BIT(12)
#define UMMU_FEAT_HD			BIT(13)
#define UMMU_FEAT_MTM			BIT(14)
#define UMMU_FEAT_TT_LE			BIT(15)
#define UMMU_FEAT_TT_BE			BIT(16)
#define UMMU_FEAT_COHERENCY		BIT(17)
#define UMMU_FEAT_BBML1			BIT(18)
#define UMMU_FEAT_BBML2			BIT(19)
#define UMMU_FEAT_VAX			BIT(20)
#define UMMU_FEAT_BTM			BIT(21)
#define UMMU_FEAT_SVA			BIT(22)
#define UMMU_FEAT_E2H			BIT(23)
#define UMMU_FEAT_MAPT			BIT(24)
#define UMMU_FEAT_RANGE_PLBI		BIT(25)
#define UMMU_FEAT_TOKEN_CHK		BIT(26)
#define UMMU_FEAT_PERMQ			BIT(27)
#define UMMU_FEAT_NESTING		BIT(28)

	u32 features;
	u32 deid_bits;
	u32 tid_bits;
	u64 pgsize_bitmap;
	u32 ias;
	u32 oas;
	u64 ptsize_bitmap;
#define UMMU_OPT_MSIPOLL		(1UL << 0)
#define UMMU_OPT_DOUBLE_PLBI		(1UL << 1)
	u32 options;

#define UMMU_MAX_ASIDS			(1UL << 16)
	unsigned int asid_bits;
#define UMMU_MAX_VMIDS			(1UL << 16)
	unsigned int vmid_bits;

	bool support_mapt;
	u32 mcmdq_log2num;
	u32 mcmdq_log2size;
	u32 evtq_log2num;
	u32 evtq_log2size;
	u32 permq_num;
	struct {
		u32 cmdq_num;
		u32 cplq_num;
	} permq_ent_num;
	u32 mtm_gp_max;
	u32 mtm_id_max;
	u16 prod_ver;
};

struct ummu_device {
	struct device *dev;
	void __iomem *base;

	struct ummu_capability cap;

	u32 nr_mcmdq;
	struct ummu_mcmdq *__percpu *mcmdq;
	struct ummu_evtq evtq;

	struct ummu_core_device core_dev;
	const struct ummu_device_helper *helper_ops;
	struct list_head list;
};

int ummu_write_reg_sync(struct ummu_device *ummu, u32 val,
			u32 reg_off, u32 ack_off);

#endif /* __UMMU_H__ */

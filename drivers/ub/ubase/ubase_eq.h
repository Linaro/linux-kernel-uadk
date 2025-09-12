/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved.
 *
 */

#ifndef __UBASE_EQ_H__
#define __UBASE_EQ_H__

#include <ub/ubase/ubase_comm_eq.h>

#include "ubase.h"

#define UBASE_MIN_IRQ_NUM 3 /* for misc aeq ceq */

#define UBASE_INT_NAME_LEN 32

#define UBASE_AE_LEVEL_NUM 4

/* Vector0 interrupt control register */
#define UBASE_MISC_VECTOR_REG_OFFSET	0x18020

enum ubase_eqc_irqn {
	UBASE_MISC_IRQ_INDEX,
};

struct ubase_irq {
	char	name[UBASE_INT_NAME_LEN];
	int	irqn;
};

struct ubase_eq_addr {
	void		*addr;
	dma_addr_t	dma_addr;
	size_t		size;
};

struct ubase_eq {
	void __iomem		*db_reg;
	struct ubase_eq_addr	addr;
	u32			eqn;
	u32			entries_num;
	u32			state;
	u32			arm_st;
	u8			eqe_size;
	u32			eq_period;
	u32			coalesce_cnt;
	int			irqn;
	u32			eqc_irqn; /* irqn for eqc */
	u32			cons_index;
};

struct ubase_ceq {
	struct ubase_dev	*udev;
	struct ubase_eq		eq;
};

struct ubase_aeq {
	struct ubase_dev	*udev;
	struct ubase_eq		eq;
	struct ubase_event_nb	cb[UBASE_AE_LEVEL_NUM];
};

struct ubase_ceqs {
	struct ubase_ceq	*ceq;
	u32			num;
};

struct ubase_irq_table {
	struct mutex			ceq_lock;
	struct ubase_ceqs		ceqs;
	struct ubase_aeq		aeq;
	struct blocking_notifier_head	nh[UBASE_DRV_MAX][UBASE_EVENT_TYPE_MAX];

	struct ubase_irq		**irqs; /* first one for misc */
	u32				irqs_num;
};

int ubase_irq_table_init(struct ubase_dev *udev);
void ubase_irq_table_uninit(struct ubase_dev *udev);

int ubase_register_ae_event(struct ubase_dev *udev);
void ubase_unregister_ae_event(struct ubase_dev *udev);

void ubase_enable_misc_vector(struct ubase_dev *udev, bool enable);

#endif

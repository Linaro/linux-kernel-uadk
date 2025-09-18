/* SPDX-License-Identifier: GPL-2.0+ */
/* Copyright(c) 2025 HiSilicon Technologies CO., Ltd. All rights reserved. */

#ifndef __UDMA_EQ_H__
#define __UDMA_EQ_H__

int udma_register_ae_event(struct auxiliary_device *adev);
void udma_unregister_ae_event(struct auxiliary_device *adev);
int udma_register_ce_event(struct auxiliary_device *adev);
void udma_unregister_crq_event(struct auxiliary_device *adev);
int udma_register_crq_event(struct auxiliary_device *adev);
int udma_register_activate_workqueue(struct udma_dev *udma_dev);
void udma_unregister_activate_workqueue(struct udma_dev *udma_dev);

static inline void udma_unregister_ce_event(struct auxiliary_device *adev)
{
	ubase_comp_unregister(adev);
}

struct udma_flush_work {
	struct udma_dev	*udev;
	struct work_struct	work;
};

#endif /* __UDMA_EQ_H__ */

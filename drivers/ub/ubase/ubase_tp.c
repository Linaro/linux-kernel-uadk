// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved.
 *
 */

#include "ubase_ctrlq_tp.h"
#include "ubase_reset.h"
#include "ubase_tp.h"

int ubase_ae_tp_flush_done(struct notifier_block *nb, unsigned long event,
			   void *data)
{
	struct ubase_event_nb *ev_nb = container_of(nb, struct ubase_event_nb, nb);
	struct ubase_dev *udev = (struct ubase_dev *)ev_nb->back;
	struct ubase_aeq_notify_info *info = data;
	u32 tp_num;

	tp_num = info->aeqe->event.queue_event.num;

	return ubase_notify_tp_fd_by_ctrlq(udev, tp_num);
}

int ubase_ae_tp_level_error(struct notifier_block *nb, unsigned long event,
			    void *data)
{
	struct ubase_event_nb *ev_nb = container_of(nb,
						    struct ubase_event_nb, nb);
	struct ubase_aeq_notify_info *info = data;
	struct ubase_dev *udev = ev_nb->back;
	u32 queue_num;

	queue_num = info->aeqe->event.queue_event.num;
	ubase_err(udev,
		  "ubase recv tp level error, event_type = 0x%x, sub_type = 0x%x, queue_num = %u\n",
		  info->event_type, info->sub_type, queue_num);

	__ubase_reset_event(udev, UBASE_UE_RESET);

	return 0;
}

int ubase_dev_init_tp_tpg(struct ubase_dev *udev)
{
	if (!ubase_utp_supported(udev) || !ubase_dev_urma_supported(udev))
		return 0;

	return ubase_dev_init_rack_tp_tpg(udev);
}

void ubase_dev_uninit_tp_tpg(struct ubase_dev *udev)
{
	if (!ubase_utp_supported(udev) || !ubase_dev_urma_supported(udev))
		return;

	ubase_dev_uninit_rack_tp_tpg(udev);
}

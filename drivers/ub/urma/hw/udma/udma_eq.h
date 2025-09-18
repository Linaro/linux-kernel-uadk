/* SPDX-License-Identifier: GPL-2.0+ */
/* Copyright(c) 2025 HiSilicon Technologies CO., Ltd. All rights reserved. */

#ifndef __UDMA_EQ_H__
#define __UDMA_EQ_H__

int udma_register_ae_event(struct auxiliary_device *adev);
void udma_unregister_ae_event(struct auxiliary_device *adev);

#endif /* __UDMA_EQ_H__ */

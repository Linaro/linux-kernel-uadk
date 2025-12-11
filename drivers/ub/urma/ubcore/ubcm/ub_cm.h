/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 *
 * Description: ub_cm header
 * Author: Chen Yutao
 * Create: 2025-01-20
 * Note:
 * History: 2025-01-20: Create file
 */

#ifndef UB_CM_H
#define UB_CM_H

#include <linux/workqueue.h>
#include <linux/cdev.h>
#include "net/ubcore_cm.h"
#include "ub_mad.h"
#include "ubcm_genl.h"

struct ubcm_context {
	struct list_head device_list;
	spinlock_t device_lock;
	struct workqueue_struct *wq;
	dev_t ubcm_devno;
	struct cdev ubcm_cdev;
	struct device *ubcm_dev;
};

struct ubcm_work {
	struct work_struct work;
	struct ubmad_send_buf *send_buf;
};

struct ubcm_context *get_ubcm_ctx(void);

/* Note: kref will increase of ubcm_device in this operation */
struct ubcm_device *ubcm_find_get_device(union ubcore_eid *eid);

void ubcm_work_handler(struct work_struct *work);

int ubcm_init(void);
void ubcm_uninit(void);

#endif /* UB_CM_H */

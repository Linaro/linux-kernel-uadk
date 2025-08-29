/* SPDX-License-Identifier: GPL-2.0+ */
/* Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved. */

#ifndef __CDMA_QUEUE_H__
#define __CDMA_QUEUE_H__

struct cdma_dev;
struct cdma_context;
struct queue_cfg;

enum cdma_queue_res_type {
	QUEUE_RES_TP,
	QUEUE_RES_JFC
};

struct cdma_queue {
	struct cdma_base_jfc *jfc;
	struct cdma_base_tp *tp;
	struct cdma_context *ctx;
	u32 id;
	struct queue_cfg cfg;
	bool is_kernel;
	struct list_head list;
	u32 jfc_id;
};

struct cdma_queue *cdma_find_queue(struct cdma_dev *cdev, u32 queue_id);
struct cdma_queue *cdma_create_queue(struct cdma_dev *cdev,
				     struct cdma_context *uctx,
				     struct queue_cfg *cfg, u32 eid_index,
				     bool is_kernel);
int cdma_delete_queue(struct cdma_dev *cdev, u32 queue_id);
void cdma_set_queue_res(struct cdma_dev *cdev, struct cdma_queue *queue,
			enum cdma_queue_res_type type, void *res);
#endif

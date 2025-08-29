/* SPDX-License-Identifier: GPL-2.0+ */
/* Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved. */

#ifndef __CDMA_EVENT_H__
#define __CDMA_EVENT_H__

#include <linux/list.h>
#include <linux/device.h>
#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/wait.h>
#include <linux/mutex.h>
#include "cdma.h"
#include "cdma_context.h"
#include "cdma_types.h"

#define MAX_EVENT_LIST_SIZE 65535

struct cdma_jfe {
	spinlock_t lock;
	struct list_head event_list;
	wait_queue_head_t poll_wait;
	struct fasync_struct *async_queue;
	uint32_t event_list_count;
};

struct cdma_jfae {
	int fd;
	struct cdma_context *ctx;
	struct cdma_file *cfile;
	struct file *file;
	struct cdma_jfe jfe;
};

struct cdma_jfe_event {
	struct list_head node;
	u32 event_type;
	u64 event_data;
	struct list_head obj_node;
	u32 *counter;
};

void cdma_jfs_async_event_cb(struct cdma_event *event, struct cdma_context *ctx);

void cdma_jfc_async_event_cb(struct cdma_event *event, struct cdma_context *ctx);

struct cdma_jfae *cdma_alloc_jfae(struct cdma_file *cfile);

void cdma_free_jfae(struct cdma_jfae *jfae);

int cdma_get_jfae(struct cdma_context *ctx);

void cdma_init_jfc_event(struct cdma_jfc_event *event, struct cdma_base_jfc *jfc);

void cdma_release_async_event(struct cdma_context *ctx, struct list_head *event_list);

void cdma_put_jfae(struct cdma_context *ctx);
#endif /* CDMA_EVENT_H */

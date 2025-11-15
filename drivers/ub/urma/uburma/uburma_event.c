// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2022-2025. All rights reserved.
 *
 * Description: uburma event implementation
 * Author: Yan Fangfang
 * Create: 2022-07-28
 * Note:
 * History: 2022-07-28: create file
 */

#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/mm_types.h>
#include <linux/uaccess.h>
#include <linux/file.h>
#include <linux/anon_inodes.h>
#include <linux/poll.h>

#include "ub/urma/ubcore_uapi.h"

#include "uburma_log.h"
#include "uburma_types.h"
#include "uburma_cmd.h"
#include "uburma_uobj.h"
#include "uburma_cmd_tlv.h"
#include "uburma_file_ops.h"

#include "uburma_event.h"

#define UBURMA_JFCE_DELETE_EVENT 0

struct uburma_jfe_event {
	struct list_head node;
	uint32_t event_type; /* support async event */
	uint64_t event_data;
	struct list_head obj_node;
	uint32_t *counter;
	uburma_jfe_event_data_free_fn event_data_free_fn;
};

void uburma_write_event_with_free_fn(
	struct uburma_jfe *jfe, uint64_t event_data, uint32_t event_type,
	struct list_head *obj_event_list, uint32_t *counter,
	uburma_jfe_event_data_free_fn event_data_free_fn)
{
	struct uburma_jfe_event *event;
	unsigned long flags;

	spin_lock_irqsave(&jfe->lock, flags);
	if (jfe->deleting) {
		spin_unlock_irqrestore(&jfe->lock, flags);
		return;
	}
	event = kmalloc(sizeof(struct uburma_jfe_event), GFP_ATOMIC);
	if (!event) {
		spin_unlock_irqrestore(&jfe->lock, flags);
		return;
	}
	event->event_data = event_data;
	event->event_type = event_type;
	event->counter = counter;
	event->event_data_free_fn = event_data_free_fn;

	list_add_tail(&event->node, &jfe->event_list);
	if (obj_event_list)
		list_add_tail(&event->obj_node, obj_event_list);
	if (jfe->async_queue)
		kill_fasync(&jfe->async_queue, SIGIO, POLL_IN);
	spin_unlock_irqrestore(&jfe->lock, flags);
	wake_up_interruptible(&jfe->poll_wait);
}

void uburma_write_event(struct uburma_jfe *jfe, uint64_t event_data,
			uint32_t event_type, struct list_head *obj_event_list,
			uint32_t *counter)
{
	uburma_write_event_with_free_fn(jfe, event_data, event_type,
					obj_event_list, counter, NULL);
}


void uburma_uninit_jfe(struct uburma_jfe *jfe)
{
	struct list_head *p, *next;
	struct uburma_jfe_event *event;

	spin_lock_irq(&jfe->lock);
	list_for_each_safe(p, next, &jfe->event_list) {
		event = list_entry(p, struct uburma_jfe_event, node);
		if (event->counter)
			list_del(&event->obj_node);
		if (event->event_data_free_fn)
			(*(event->event_data_free_fn))(event->event_data);
		list_del(&event->node);
		kfree(event);
	}
	spin_unlock_irq(&jfe->lock);
}

const struct file_operations uburma_jfce_fops = {
	.owner = THIS_MODULE,
};
void uburma_init_jfe(struct uburma_jfe *jfe)
{
	spin_lock_init(&jfe->lock);
	INIT_LIST_HEAD(&jfe->event_list);
	init_waitqueue_head(&jfe->poll_wait);
	jfe->async_queue = NULL;
}

const struct file_operations uburma_jfae_fops = {
	.owner = THIS_MODULE,
};

void uburma_release_comp_event(struct uburma_jfce_uobj *jfce,
			       struct list_head *event_list)
{
	struct uburma_jfe *jfe = &jfce->jfe;
	struct uburma_jfe_event *event, *tmp;

	spin_lock_irq(&jfe->lock);
	list_for_each_entry_safe(event, tmp, event_list, obj_node) {
		list_del(&event->node);
		kfree(event);
	}
	spin_unlock_irq(&jfe->lock);
}

void uburma_release_async_event(struct uburma_file *ufile,
				struct list_head *event_list)
{
	struct uburma_jfae_uobj *jfae = ufile->ucontext->jfae;
	struct uburma_jfe *jfe = &jfae->jfe;
	struct uburma_jfe_event *event, *tmp;

	spin_lock_irq(&jfe->lock);
	list_for_each_entry_safe(event, tmp, event_list, obj_node) {
		list_del(&event->node);
		kfree(event);
	}
	spin_unlock_irq(&jfe->lock);
}

const struct file_operations uburma_notifier_fops = {
	.owner = THIS_MODULE,
};


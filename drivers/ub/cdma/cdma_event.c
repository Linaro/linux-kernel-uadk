// SPDX-License-Identifier: GPL-2.0+
/* Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved. */

#define pr_fmt(fmt) "CDMA: " fmt
#define dev_fmt pr_fmt

#include <linux/file.h>
#include <linux/anon_inodes.h>
#include <linux/rcupdate.h>
#include <linux/ioctl.h>
#include <linux/wait.h>
#include <linux/poll.h>
#include <uapi/ub/cdma/cdma_abi.h>
#include "cdma_uobj.h"
#include "cdma_event.h"

static __poll_t cdma_jfe_poll(struct cdma_jfe *jfe, struct file *filp,
			      struct poll_table_struct *wait)
{
	__poll_t flag = 0;

	poll_wait(filp, &jfe->poll_wait, wait);

	spin_lock_irq(&jfe->lock);
	if (!list_empty(&jfe->event_list))
		flag = EPOLLIN | EPOLLRDNORM;

	spin_unlock_irq(&jfe->lock);

	return flag;
}

static u32 cdma_read_jfe_event(struct cdma_jfe *jfe, u32 max_event_cnt,
			       struct list_head *event_list)
{
	struct cdma_jfe_event *event;
	struct list_head *next;
	struct list_head *p;
	u32 cnt = 0;

	if (!max_event_cnt)
		return 0;

	spin_lock_irq(&jfe->lock);

	list_for_each_safe(p, next, &jfe->event_list) {
		event = list_entry(p, struct cdma_jfe_event, node);
		if (event->counter) {
			++(*event->counter);
			list_del(&event->obj_node);
		}
		list_del(p);
		if (jfe->event_list_count > 0)
			jfe->event_list_count--;
		list_add_tail(p, event_list);
		cnt++;
		if (cnt == max_event_cnt)
			break;
	}
	spin_unlock_irq(&jfe->lock);

	return cnt;
}

static int cdma_wait_event(struct cdma_jfe *jfe, bool nonblock,
			   u32 max_event_cnt, u32 *event_cnt,
			   struct list_head *event_list)
{
	int ret;

	*event_cnt = 0;
	spin_lock_irq(&jfe->lock);
	while (list_empty(&jfe->event_list)) {
		spin_unlock_irq(&jfe->lock);
		if (nonblock)
			return -EAGAIN;

		ret = wait_event_interruptible(jfe->poll_wait,
					       !list_empty(&jfe->event_list));
		if (ret)
			return ret;

		spin_lock_irq(&jfe->lock);
		if (list_empty(&jfe->event_list)) {
			spin_unlock_irq(&jfe->lock);
			return -EIO;
		}
	}
	spin_unlock_irq(&jfe->lock);
	*event_cnt = cdma_read_jfe_event(jfe, max_event_cnt, event_list);

	return 0;
}

static void cdma_write_event(struct cdma_jfe *jfe, u64 event_data,
			     u32 event_type, struct list_head *obj_event_list,
			     u32 *counter)
{
	struct cdma_jfe_event *event;
	unsigned long flags;

	event = kzalloc(sizeof(*event), GFP_ATOMIC);
	if (event == NULL)
		return;

	spin_lock_irqsave(&jfe->lock, flags);
	INIT_LIST_HEAD(&event->obj_node);
	event->event_type = event_type;
	event->event_data = event_data;
	event->counter = counter;
	list_add_tail(&event->node, &jfe->event_list);
	if (obj_event_list)
		list_add_tail(&event->obj_node, obj_event_list);
	if (jfe->async_queue)
		kill_fasync(&jfe->async_queue, SIGIO, POLL_IN);
	jfe->event_list_count++;
	spin_unlock_irqrestore(&jfe->lock, flags);
	wake_up_interruptible(&jfe->poll_wait);
}

static void cdma_init_jfe(struct cdma_jfe *jfe)
{
	spin_lock_init(&jfe->lock);
	INIT_LIST_HEAD(&jfe->event_list);
	init_waitqueue_head(&jfe->poll_wait);
	jfe->async_queue = NULL;
	jfe->event_list_count = 0;
}

static void cdma_uninit_jfe(struct cdma_jfe *jfe)
{
	struct cdma_jfe_event *event;
	struct list_head *p, *next;

	spin_lock_irq(&jfe->lock);
	list_for_each_safe(p, next, &jfe->event_list) {
		event = list_entry(p, struct cdma_jfe_event, node);
		if (event->counter)
			list_del(&event->obj_node);
		kfree(event);
	}
	spin_unlock_irq(&jfe->lock);
}

static void cdma_write_async_event(struct cdma_context *ctx, u64 event_data,
				   u32 type, struct list_head *obj_event_list,
				   u32 *counter)
{
	struct cdma_jfae *jfae;

	rcu_read_lock();
	jfae = (struct cdma_jfae *)(rcu_dereference(ctx->jfae));
	if (!jfae)
		goto err_free_rcu;

	if (jfae->jfe.event_list_count >= MAX_EVENT_LIST_SIZE) {
		pr_debug("event list overflow, and this write will be discarded.\n");
		goto err_free_rcu;
	}

	cdma_write_event(&jfae->jfe, event_data, type, obj_event_list, counter);

err_free_rcu:
	rcu_read_unlock();
}

void cdma_jfs_async_event_cb(struct cdma_event *event, struct cdma_context *ctx)
{
	struct cdma_jfs_event *jfs_event;

	jfs_event = &event->element.jfs->jfs_event;
	cdma_write_async_event(ctx, event->element.jfs->cfg.queue_id,
			       event->event_type, &jfs_event->async_event_list,
			       &jfs_event->async_events_reported);
}

void cdma_jfc_async_event_cb(struct cdma_event *event, struct cdma_context *ctx)
{
	struct cdma_jfc_event *jfc_event;

	jfc_event = &event->element.jfc->jfc_event;
	cdma_write_async_event(ctx, event->element.jfc->jfc_cfg.queue_id,
			       event->event_type, &jfc_event->async_event_list,
			       &jfc_event->async_events_reported);
}

static inline void cdma_set_async_event(struct cdma_cmd_async_event *async_event,
					const struct cdma_jfe_event *event)
{
	async_event->event_data = event->event_data;
	async_event->event_type = event->event_type;
}

static int cdma_get_async_event(struct cdma_jfae *jfae, struct file *filp,
				unsigned long arg)
{
	struct cdma_cmd_async_event async_event = { 0 };
	struct cdma_jfe_event *event;
	struct list_head event_list;
	u32 event_cnt;
	int ret;

	if (!arg) {
		pr_err("invalid jfae arg.\n");
		return -EINVAL;
	}

	INIT_LIST_HEAD(&event_list);
	ret = cdma_wait_event(&jfae->jfe, filp->f_flags & O_NONBLOCK, 1,
			      &event_cnt, &event_list);
	if (ret < 0) {
		pr_err("wait event failed, ret = %d.\n", ret);
		return ret;
	}
	event = list_first_entry(&event_list, struct cdma_jfe_event, node);
	if (event == NULL)
		return -EIO;

	cdma_set_async_event(&async_event, event);
	list_del(&event->node);
	kfree(event);

	if (event_cnt > 0) {
		ret = (int)copy_to_user((void *)arg, &async_event,
					sizeof(async_event));
		if (ret) {
			pr_err("dev copy to user failed, ret = %d\n", ret);
			return -EFAULT;
		}
	}

	return 0;
}

static __poll_t cdma_jfae_poll(struct file *filp, struct poll_table_struct *wait)
{
	struct cdma_jfae *jfae = (struct cdma_jfae *)filp->private_data;

	if (!jfae || !jfae->cfile || !jfae->cfile->cdev)
		return POLLERR;

	return cdma_jfe_poll(&jfae->jfe, filp, wait);
}

static long cdma_jfae_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct cdma_jfae *jfae = (struct cdma_jfae *)filp->private_data;
	unsigned int nr;
	int ret;

	if (!jfae)
		return -EINVAL;

	nr = (unsigned int)_IOC_NR(cmd);

	switch (nr) {
	case JFAE_CMD_GET_ASYNC_EVENT:
		ret = cdma_get_async_event(jfae, filp, arg);
		break;
	default:
		dev_err(jfae->cfile->cdev->dev, "nr = %u.\n", nr);
		ret = -ENOIOCTLCMD;
		break;
	}

	return (long)ret;
}

static int cdma_delete_jfae(struct inode *inode, struct file *filp)
{
	struct cdma_file *cfile;
	struct cdma_jfae *jfae;

	if (!filp || !filp->private_data)
		return 0;

	jfae = (struct cdma_jfae *)filp->private_data;
	cfile = jfae->cfile;
	if (!cfile)
		return 0;

	if (!mutex_trylock(&cfile->ctx_mutex))
		return -ENOLCK;
	jfae->ctx->jfae = NULL;
	cdma_uninit_jfe(&jfae->jfe);
	kfree(jfae);
	filp->private_data = NULL;
	mutex_unlock(&cfile->ctx_mutex);
	cdma_close_uobj_fd(cfile);

	pr_debug("jfae is release.\n");
	return 0;
}

static int cdma_jfae_fasync(int fd, struct file *filp, int on)
{
	struct cdma_jfae *jfae = (struct cdma_jfae *)filp->private_data;
	int ret;

	if (!jfae)
		return -EINVAL;

	spin_lock_irq(&jfae->jfe.lock);
	ret = fasync_helper(fd, filp, on, &jfae->jfe.async_queue);
	spin_unlock_irq(&jfae->jfe.lock);

	return ret;
}

const struct file_operations cdma_jfae_fops = {
	.owner = THIS_MODULE,
	.poll = cdma_jfae_poll,
	.unlocked_ioctl = cdma_jfae_ioctl,
	.release = cdma_delete_jfae,
	.fasync = cdma_jfae_fasync,
};

struct cdma_jfae *cdma_alloc_jfae(struct cdma_file *cfile)
{
	struct cdma_jfae *jfae;
	struct file *file;
	int fd;

	if (!cfile)
		return NULL;

	fd = get_unused_fd_flags(O_RDWR | O_CLOEXEC);
	if (fd < 0)
		return NULL;

	jfae = kzalloc(sizeof(*jfae), GFP_KERNEL);
	if (!jfae)
		goto err_put_unused_fd;

	file = anon_inode_getfile("[jfae]", &cdma_jfae_fops, jfae,
				  O_RDWR | O_CLOEXEC);
	if (IS_ERR(file))
		goto err_free_jfae;

	cdma_init_jfe(&jfae->jfe);
	jfae->fd = fd;
	jfae->file = file;
	jfae->cfile = cfile;
	fd_install(fd, file);

	return jfae;

err_free_jfae:
	kfree(jfae);
err_put_unused_fd:
	put_unused_fd(fd);

	return NULL;
}

void cdma_free_jfae(struct cdma_jfae *jfae)
{
	if (!jfae)
		return;

	fput(jfae->file);
	put_unused_fd(jfae->fd);
}

int cdma_get_jfae(struct cdma_context *ctx)
{
	struct cdma_jfae *jfae;
	struct file *file;

	if (!ctx)
		return -EINVAL;

	jfae = (struct cdma_jfae *)ctx->jfae;
	if (!jfae)
		return -EINVAL;

	file = fget(jfae->fd);
	if (!file)
		return -ENOENT;

	if (file->private_data != jfae) {
		fput(file);
		return -EBADF;
	}

	return 0;
}

void cdma_init_jfc_event(struct cdma_jfc_event *event, struct cdma_base_jfc *jfc)
{
	event->async_events_reported = 0;
	INIT_LIST_HEAD(&event->async_event_list);
	event->jfc = jfc;
}

void cdma_release_async_event(struct cdma_context *ctx, struct list_head *event_list)
{
	struct cdma_jfe_event *event, *tmp;
	struct cdma_jfae *jfae;
	struct cdma_jfe *jfe;

	if (!ctx || !ctx->jfae)
		return;

	jfae = (struct cdma_jfae *)ctx->jfae;
	jfe = &jfae->jfe;
	spin_lock_irq(&jfe->lock);
	list_for_each_entry_safe(event, tmp, event_list, obj_node) {
		list_del(&event->node);
		kfree(event);
	}
	spin_unlock_irq(&jfe->lock);
	fput(jfae->file);
}

void cdma_put_jfae(struct cdma_context *ctx)
{
	struct cdma_jfae *jfae;

	if (!ctx)
		return;

	jfae = (struct cdma_jfae *)ctx->jfae;
	if (!jfae)
		return;

	if (!jfae->file)
		return;

	fput(jfae->file);
}

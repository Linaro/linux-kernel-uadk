// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2021-2025. All rights reserved.
 *
 * Description: uburma cmd implementation
 * Author: Qian Guoxin
 * Create: 2021-08-04
 * Note:
 * History: 2021-08-04: Create file
 * History: 2022-07-25: Yan Fangfang Change the prefix uburma_ioctl_ to uburma_cmd_
 */

#include <linux/fs.h>
#include <linux/uaccess.h>

#include "ub/urma/ubcore_uapi.h"
#include "ub/urma/ubcore_types.h"
#include "uburma_log.h"
#include "uburma_types.h"
#include "uburma_event.h"
#include "uburma_file_ops.h"
#include "uburma_uobj.h"
#include "uburma_cmd_tlv.h"

#include "uburma_cmd.h"

#define UBURMA_INVALID_TPN UINT_MAX
#define UBURMA_CREATE_JETTY_ARG_IN_RC_SHARE_TP_SHIFT 11

void uburma_cmd_inc(struct uburma_device *ubu_dev)
{
	atomic_inc(&ubu_dev->cmdcnt);
}

void uburma_cmd_dec(struct uburma_device *ubu_dev)
{
	if (atomic_dec_and_test(&ubu_dev->cmdcnt))
		complete(&ubu_dev->cmddone);
}

void uburma_cmd_flush(struct uburma_device *ubu_dev)
{
	uburma_cmd_dec(ubu_dev);
	wait_for_completion(&ubu_dev->cmddone);
}

static inline void fill_udata(struct ubcore_udata *out,
			      struct ubcore_ucontext *ctx,
			      struct uburma_cmd_udrv_priv *udata)
{
	out->uctx = ctx;
	out->udrv_data = (struct ubcore_udrv_priv *)(void *)udata;
}

static int uburma_cmd_create_ctx(struct ubcore_device *ubc_dev,
				 struct uburma_file *file,
				 struct uburma_cmd_hdr *hdr)
{
	struct ubcore_ucontext *ucontext;
	struct uburma_cmd_create_ctx arg;
	struct uburma_uobj *uobj;
	struct uburma_jfae_uobj *jfae;
	union ubcore_eid eid;
	int ret;

	ret = uburma_tlv_parse(hdr, (void *)&arg);
	if (ret != 0)
		return ret;

	down_write(&file->ucontext_rwsem);

	if (file->ucontext) {
		up_write(&file->ucontext_rwsem);
		uburma_log_err(
			"ucontext eixt, should not create ctx in same fd.\n");
		return -EEXIST;
	}

	(void)memcpy(eid.raw, arg.in.eid, UBCORE_EID_SIZE);
	ucontext = ubcore_alloc_ucontext(
		ubc_dev, arg.in.eid_index,
		(struct ubcore_udrv_priv *)(void *)&arg.udata);
	if (IS_ERR_OR_NULL(ucontext)) {
		up_write(&file->ucontext_rwsem);
		return PTR_ERR(ucontext);
	}
	ucontext->eid = eid;
	uobj = uobj_alloc(UOBJ_CLASS_JFAE, file);
	if (IS_ERR_OR_NULL(uobj)) {
		ret = PTR_ERR(uobj);
		goto free_ctx;
	}

	jfae = container_of(uobj, struct uburma_jfae_uobj, uobj);
	uburma_init_jfae(jfae, ubc_dev);
	ucontext->jfae = uobj;
	arg.out.async_fd = uobj->id;
	file->ucontext = ucontext;

	ret = uburma_tlv_append(hdr, (void *)&arg);
	if (ret != 0)
		goto free_jfae;

	uobj_alloc_commit(uobj);
	up_write(&file->ucontext_rwsem);
	uburma_log_debug("uburma create context success.\n");
	return ret;

free_jfae:
	uobj_alloc_abort(uobj);
free_ctx:
	ubcore_free_ucontext(ubc_dev, ucontext);
	file->ucontext = NULL;
	up_write(&file->ucontext_rwsem);
	return ret;
}

static void uburma_fill_attr(struct ubcore_seg_cfg *cfg,
			     struct uburma_cmd_register_seg *arg)
{
	cfg->va = arg->in.va;
	cfg->len = arg->in.len;
	cfg->flag.value = arg->in.flag;
	cfg->token_value.token = arg->in.token;
	cfg->iova = arg->in.va;
}

static int uburma_cmd_register_seg(struct ubcore_device *ubc_dev,
				   struct uburma_file *file,
				   struct uburma_cmd_hdr *hdr)
{
	struct uburma_cmd_register_seg arg;
	struct ubcore_seg_cfg cfg = { 0 };
	struct ubcore_target_seg *seg;
	struct ubcore_udata udata = { 0 };
	struct uburma_uobj *uobj;
	struct uburma_uobj *token_id_uobj;
	int ret;

	ret = uburma_tlv_parse(hdr, (void *)&arg);
	if (ret != 0)
		return ret;

	token_id_uobj = uobj_get_read(UOBJ_CLASS_TOKEN,
				      (int)arg.in.token_id_handle, file);
	if (!IS_ERR_OR_NULL(token_id_uobj))
		cfg.token_id = (struct ubcore_token_id *)token_id_uobj->object;

	uburma_fill_attr(&cfg, &arg);
	cfg.eid_index = file->ucontext->eid_index;
	fill_udata(&udata, file->ucontext, &arg.udata);

	uobj = uobj_alloc(UOBJ_CLASS_SEG, file);
	if (IS_ERR_OR_NULL(uobj)) {
		uburma_log_err("UOBJ_CLASS_SEG alloc fail!\n");
		ret = -ENOMEM;
		goto err_put_token_id;
	}

	seg = ubcore_register_seg(ubc_dev, &cfg, &udata);
	if (IS_ERR_OR_NULL(seg)) {
		uburma_log_err_rl("ubcore_register_seg failed.\n");
		ret = PTR_ERR(seg);
		goto err_free_uobj;
	}
	uobj->object = seg;
	arg.out.token_id = seg->seg.token_id;
	arg.out.handle = (uint64_t)uobj->id;

	ret = uburma_tlv_append(hdr, (void *)&arg);
	if (ret != 0)
		goto err_delete_seg;

	if (!IS_ERR_OR_NULL(token_id_uobj))
		uobj_put_read(token_id_uobj);
	uobj_alloc_commit(uobj);
	return 0;

err_delete_seg:
	ubcore_unregister_seg(seg);
err_free_uobj:
	uobj_alloc_abort(uobj);
err_put_token_id:
	if (!IS_ERR_OR_NULL(token_id_uobj))
		uobj_put_read(token_id_uobj);
	return ret;
}

static int uburma_cmd_unregister_seg(struct ubcore_device *ubc_dev,
				     struct uburma_file *file,
				     struct uburma_cmd_hdr *hdr)
{
	struct uburma_cmd_unregister_seg arg;
	struct uburma_uobj *uobj;
	int ret;

	ret = uburma_tlv_parse(hdr, (void *)&arg);
	if (ret != 0)
		return ret;

	uobj = uobj_get_del(UOBJ_CLASS_SEG, arg.in.handle, file);
	if (IS_ERR_OR_NULL(uobj)) {
		uburma_log_err("failed to find registered seg.\n");
		return -EINVAL;
	}
	ret = uobj_remove_commit(uobj);
	if (ret != 0)
		uburma_log_err("ubcore_unregister_seg failed.\n");

	uobj_put_del(uobj);
	return ret;
}

static void uburma_write_async_event(struct ubcore_ucontext *ctx,
				     uint64_t event_data, uint32_t event_type,
				     struct list_head *obj_event_list,
				     uint32_t *counter)
{
	struct uburma_jfae_uobj *jfae;

	rcu_read_lock();
	jfae = rcu_dereference(ctx->jfae);
	if (!jfae) {
		rcu_read_unlock();
		return;
	}
	uburma_write_event(&jfae->jfe, event_data, event_type, obj_event_list,
			   counter);
	rcu_read_unlock();
}

void uburma_jfc_event_cb(struct ubcore_event *event,
			 struct ubcore_ucontext *ctx)
{
	struct uburma_jfc_uobj *jfc_uobj;

	if (!event->element.jfc)
		return;

	jfc_uobj = (struct uburma_jfc_uobj *)
			   event->element.jfc->jfc_cfg.jfc_context;
	uburma_write_async_event(ctx, event->element.jfc->urma_jfc,
				 event->event_type, &jfc_uobj->async_event_list,
				 &jfc_uobj->async_events_reported);
}

void uburma_jfs_event_cb(struct ubcore_event *event,
			 struct ubcore_ucontext *ctx)
{
	struct uburma_jfs_uobj *jfs_uobj;

	if (!event->element.jfs)
		return;

	jfs_uobj = (struct uburma_jfs_uobj *)
			   event->element.jfs->jfs_cfg.jfs_context;
	uburma_write_async_event(ctx, event->element.jfs->urma_jfs,
				 event->event_type, &jfs_uobj->async_event_list,
				 &jfs_uobj->async_events_reported);
}

void uburma_jfr_event_cb(struct ubcore_event *event,
			 struct ubcore_ucontext *ctx)
{
	struct uburma_jfr_uobj *jfr_uobj;

	if (!event->element.jfr)
		return;

	jfr_uobj = (struct uburma_jfr_uobj *)
			   event->element.jfr->jfr_cfg.jfr_context;
	uburma_write_async_event(ctx, event->element.jfr->urma_jfr,
				 event->event_type, &jfr_uobj->async_event_list,
				 &jfr_uobj->async_events_reported);
}

void uburma_jetty_event_cb(struct ubcore_event *event,
			   struct ubcore_ucontext *ctx)
{
	struct uburma_jetty_uobj *jetty_uobj;

	if (!event->element.jetty)
		return;

	jetty_uobj = (struct uburma_jetty_uobj *)
			     event->element.jetty->jetty_cfg.jetty_context;
	uburma_write_async_event(ctx, event->element.jetty->urma_jetty,
				 event->event_type,
				 &jetty_uobj->async_event_list,
				 &jetty_uobj->async_events_reported);
}

void uburma_jetty_grp_event_cb(struct ubcore_event *event,
			       struct ubcore_ucontext *ctx)
{
	struct uburma_jetty_grp_uobj *jetty_grp_uobj;

	if (!event->element.jetty_grp)
		return;

	jetty_grp_uobj =
		(struct uburma_jetty_grp_uobj *)
			event->element.jetty_grp->jetty_grp_cfg.user_ctx;
	uburma_write_async_event(ctx, event->element.jetty_grp->urma_jetty_grp,
				 event->event_type,
				 &jetty_grp_uobj->async_event_list,
				 &jetty_grp_uobj->async_events_reported);
}

static int uburma_cmd_create_jfs(struct ubcore_device *ubc_dev,
				 struct uburma_file *file,
				 struct uburma_cmd_hdr *hdr)
{
	struct uburma_cmd_create_jfs arg;
	struct ubcore_jfs_cfg cfg = { 0 };
	struct ubcore_udata udata;
	struct uburma_jfs_uobj *jfs_uobj;
	struct uburma_uobj *jfc_uobj;
	struct ubcore_jfs *jfs;
	int ret;

	ret = uburma_tlv_parse(hdr, (void *)&arg);
	if (ret != 0)
		return ret;

	cfg.depth = arg.in.depth;
	cfg.flag.value = arg.in.flag;
	cfg.eid_index = file->ucontext->eid_index;
	cfg.trans_mode = arg.in.trans_mode;
	cfg.max_sge = arg.in.max_sge;
	cfg.max_rsge = arg.in.max_rsge;
	cfg.max_inline_data = arg.in.max_inline_data;
	cfg.rnr_retry = arg.in.rnr_retry;
	cfg.err_timeout = arg.in.err_timeout;
	cfg.priority = arg.in.priority;

	jfs_uobj = (struct uburma_jfs_uobj *)uobj_alloc(UOBJ_CLASS_JFS, file);
	if (IS_ERR_OR_NULL(jfs_uobj)) {
		uburma_log_err("UOBJ_CLASS_JFS alloc fail!\n");
		return -ENOMEM;
	}
	jfs_uobj->async_events_reported = 0;
	INIT_LIST_HEAD(&jfs_uobj->async_event_list);
	cfg.jfs_context = jfs_uobj;

	jfc_uobj = uobj_get_read(UOBJ_CLASS_JFC, arg.in.jfc_handle, file);
	if (IS_ERR_OR_NULL(jfc_uobj)) {
		uburma_log_err("failed to find jfc, jfc_handle:%llu.\n",
			       arg.in.jfc_handle);
		ret = -EINVAL;
		goto err_alloc_abort;
	}
	cfg.jfc = jfc_uobj->object;
	fill_udata(&udata, file->ucontext, &arg.udata);

	jfs = ubcore_create_jfs(ubc_dev, &cfg, uburma_jfs_event_cb, &udata);
	if (IS_ERR_OR_NULL(jfs)) {
		uburma_log_err("create jfs or get jfs_id failed.\n");
		ret = PTR_ERR(jfs);
		goto err_put_jfc;
	}
	jfs_uobj->uobj.object = jfs;
	jfs->urma_jfs = arg.in.urma_jfs;

	/* Do not release jfae fd until jfs is destroyed */
	ret = uburma_get_jfae(file);
	if (ret != 0)
		goto err_delete_jfs;

	arg.out.id = jfs->jfs_id.id;
	arg.out.depth = jfs->jfs_cfg.depth;
	arg.out.max_sge = jfs->jfs_cfg.max_sge;
	arg.out.max_rsge = jfs->jfs_cfg.max_rsge;
	arg.out.max_inline_data = jfs->jfs_cfg.max_inline_data;
	arg.out.handle = jfs_uobj->uobj.id;

	ret = uburma_tlv_append(hdr, (void *)&arg);
	if (ret != 0)
		goto err_put_jfae;

	uobj_put_read(jfc_uobj);
	uobj_alloc_commit(&jfs_uobj->uobj);
	return 0;

err_put_jfae:
	uburma_put_jfae(file);
err_delete_jfs:
	ubcore_delete_jfs(jfs);
err_put_jfc:
	uobj_put_read(jfc_uobj);
err_alloc_abort:
	uobj_alloc_abort(&jfs_uobj->uobj);
	return ret;
}

static int uburma_cmd_modify_jfs(struct ubcore_device *ubc_dev,
				 struct uburma_file *file,
				 struct uburma_cmd_hdr *hdr)
{
	struct uburma_cmd_modify_jfs arg;
	struct ubcore_jfs_attr attr = { 0 };
	struct uburma_uobj *uobj;
	struct ubcore_udata udata;
	struct ubcore_jfs *jfs;
	int ret;

	ret = uburma_tlv_parse(hdr, (void *)&arg);
	if (ret != 0)
		return ret;

	attr.mask = arg.in.mask;
	attr.state = arg.in.state;
	fill_udata(&udata, file->ucontext, &arg.udata);

	uobj = uobj_get_write(UOBJ_CLASS_JFS, arg.in.handle, file);
	if (IS_ERR_OR_NULL(uobj)) {
		uburma_log_err("failed to find jfs.\n");
		return -EINVAL;
	}

	jfs = (struct ubcore_jfs *)uobj->object;
	ret = ubcore_modify_jfs(jfs, &attr, &udata);
	if (ret != 0) {
		uobj_put_write(uobj);
		uburma_log_err("modify jfs failed, ret:%d.\n", ret);
		return ret;
	}

	ret = uburma_tlv_append(hdr, (void *)&arg);
	uobj_put_write(uobj);
	return ret;
}

static int uburma_cmd_query_jfs(struct ubcore_device *ubc_dev,
				struct uburma_file *file,
				struct uburma_cmd_hdr *hdr)
{
	struct uburma_cmd_query_jfs arg;
	struct ubcore_jfs_attr attr = { 0 };
	struct ubcore_jfs_cfg cfg = { 0 };
	struct uburma_uobj *uobj;
	struct ubcore_jfs *jfs;
	int ret;

	ret = uburma_tlv_parse(hdr, (void *)&arg);
	if (ret != 0)
		return ret;

	uobj = uobj_get_read(UOBJ_CLASS_JFS, arg.in.handle, file);
	if (IS_ERR_OR_NULL(uobj)) {
		uburma_log_err("failed to find jfs.\n");
		return -EINVAL;
	}

	jfs = (struct ubcore_jfs *)uobj->object;
	ret = ubcore_query_jfs(jfs, &cfg, &attr);
	if (ret != 0) {
		uobj_put_read(uobj);
		uburma_log_err("query jfs failed, ret:%d.\n", ret);
		return ret;
	}

	arg.out.depth = cfg.depth;
	arg.out.flag = cfg.flag.value;
	arg.out.trans_mode = (uint32_t)cfg.trans_mode;
	arg.out.priority = cfg.priority;
	arg.out.max_sge = cfg.max_sge;
	arg.out.max_rsge = cfg.max_rsge;
	arg.out.max_inline_data = cfg.max_inline_data;
	arg.out.rnr_retry = cfg.rnr_retry;
	arg.out.err_timeout = cfg.err_timeout;
	arg.out.state = (uint32_t)attr.state;

	ret = uburma_tlv_append(hdr, (void *)&arg);
	uobj_put_read(uobj);
	return ret;
}

static int uburma_cmd_delete_jfs(struct ubcore_device *ubc_dev,
				 struct uburma_file *file,
				 struct uburma_cmd_hdr *hdr)
{
	struct uburma_cmd_delete_jfs arg;
	struct uburma_jfs_uobj *jfs_uobj;
	struct uburma_uobj *uobj;
	int ret;

	ret = uburma_tlv_parse(hdr, (void *)&arg);
	if (ret != 0)
		return ret;

	uobj = uobj_get_del(UOBJ_CLASS_JFS, arg.in.handle, file);
	if (IS_ERR_OR_NULL(uobj)) {
		uburma_log_err("failed to find jfs");
		return -EINVAL;
	}

	/* To get async_events_reported after obj removed. */
	uobj_get(uobj);
	jfs_uobj = container_of(uobj, struct uburma_jfs_uobj, uobj);

	ret = uobj_remove_commit(uobj);
	if (ret != 0) {
		uburma_log_err("delete jfs failed, ret:%d.\n", ret);
		uobj_put(uobj);
		uobj_put_del(uobj);
		return ret;
	}

	arg.out.async_events_reported = jfs_uobj->async_events_reported;
	uobj_put(uobj);
	uobj_put_del(uobj);
	return uburma_tlv_append(hdr, (void *)&arg);
}

static int uburma_cmd_delete_jfs_batch(struct ubcore_device *ubc_dev,
				       struct uburma_file *file,
				       struct uburma_cmd_hdr *hdr)
{
	struct uburma_jfs_uobj *jfs_uobj = NULL;
	struct uburma_cmd_delete_jfs_batch arg;
	struct uburma_uobj **uobj_arr = NULL;
	uint32_t async_events_reported = 0;
	struct uburma_uobj *uobj = NULL;
	uint32_t bad_jfs_index = 0;
	uint64_t *jfs_arr = NULL;
	uint32_t arr_num;
	int ret_bad;
	int ret;
	int i;

	ret = uburma_tlv_parse(hdr, (void *)&arg);
	if (ret != 0)
		return ret;

	arr_num = arg.in.jfs_num;
	jfs_arr = kcalloc(arr_num, sizeof(uint64_t), GFP_KERNEL);
	if (!jfs_arr)
		return -ENOMEM;

	ret = uburma_copy_from_user((void *)jfs_arr,
				    (void __user *)arg.in.jfs_ptr,
				    arr_num * sizeof(uint64_t));
	if (ret != 0)
		goto free_jfs_arr;

	uobj_arr = kcalloc(arr_num, sizeof(struct uburma_uobj *), GFP_KERNEL);
	if (!uobj_arr) {
		ret = -ENOMEM;
		goto free_jfs_arr;
	}

	for (i = 0; i < arr_num; ++i) {
		uobj = uobj_get_del(UOBJ_CLASS_JFS, jfs_arr[i], file);
		uobj_arr[i] = uobj;
		if (IS_ERR(uobj)) {
			uburma_log_err("failed to find jfs, index is %d.\n", i);
			ret = -EINVAL;
			goto free_uobj_arr;
		}
		/* To get events_reported after obj removed. */
		uobj_get(uobj);
		jfs_uobj = container_of(uobj, struct uburma_jfs_uobj, uobj);
		async_events_reported += jfs_uobj->async_events_reported;
	}

	ret = uobj_remove_commit_batch(uobj_arr, arr_num, &bad_jfs_index);
	if (ret != 0)
		uburma_log_err("delete jfs failed, ret:%d.\n", ret);

	arg.out.async_events_reported = async_events_reported;
	arg.out.bad_jfs_index = bad_jfs_index;

	uobj_put_batch(uobj_arr, arr_num);
	uobj_put_del_batch(uobj_arr, arr_num);

	ret_bad = uburma_tlv_append(hdr, (void *)&arg);
	if (ret_bad != 0) {
		uburma_log_err("uburma tlv append error, ret:%d.\n", ret_bad);
		ret = ret_bad;
	}

free_uobj_arr:
	kfree(uobj_arr);
free_jfs_arr:
	kfree(jfs_arr);
	return ret;
}

static int uburma_cmd_create_jfr(struct ubcore_device *ubc_dev,
				 struct uburma_file *file,
				 struct uburma_cmd_hdr *hdr)
{
	struct uburma_cmd_create_jfr arg;
	struct uburma_uobj *jfc_uobj;
	struct uburma_jfr_uobj *jfr_uobj;
	struct ubcore_jfr_cfg cfg = { 0 };
	struct ubcore_udata udata;
	struct ubcore_jfr *jfr;
	int ret;

	ret = uburma_tlv_parse(hdr, (void *)&arg);
	if (ret != 0)
		return ret;

	cfg.id = arg.in.id;
	cfg.depth = arg.in.depth;
	cfg.eid_index = file->ucontext->eid_index;
	cfg.flag.value = arg.in.flag;
	cfg.max_sge = arg.in.max_sge;
	cfg.min_rnr_timer = arg.in.min_rnr_timer;
	cfg.trans_mode = arg.in.trans_mode;
	cfg.token_value.token = arg.in.token;
	fill_udata(&udata, file->ucontext, &arg.udata);

	jfr_uobj = (struct uburma_jfr_uobj *)uobj_alloc(UOBJ_CLASS_JFR, file);
	if (IS_ERR_OR_NULL(jfr_uobj)) {
		uburma_log_err("UOBJ_CLASS_JFR alloc fail!\n");
		return -ENOMEM;
	}
	jfr_uobj->async_events_reported = 0;
	INIT_LIST_HEAD(&jfr_uobj->async_event_list);
	cfg.jfr_context = jfr_uobj;

	jfc_uobj = uobj_get_read(UOBJ_CLASS_JFC, arg.in.jfc_handle, file);
	if (IS_ERR_OR_NULL(jfc_uobj)) {
		uburma_log_err("failed to find jfc, jfc_handle:%llu.\n",
			       arg.in.jfc_handle);
		ret = -EINVAL;
		goto err_alloc_abort;
	}
	cfg.jfc = jfc_uobj->object;

	jfr = ubcore_create_jfr(ubc_dev, &cfg, uburma_jfr_event_cb, &udata);
	if (IS_ERR_OR_NULL(jfr)) {
		uburma_log_err("create jfr or get jfr_id failed.\n");
		ret = PTR_ERR(jfr);
		goto err_put_jfc;
	}
	jfr_uobj->uobj.object = jfr;
	jfr->urma_jfr = arg.in.urma_jfr;

	/* Do not release jfae fd until jfr is destroyed */
	ret = uburma_get_jfae(file);
	if (ret != 0)
		goto err_delete_jfr;

	arg.out.id = jfr->jfr_id.id;
	arg.out.depth = jfr->jfr_cfg.depth;
	arg.out.max_sge = jfr->jfr_cfg.max_sge;
	arg.out.handle = jfr_uobj->uobj.id;

	ret = uburma_tlv_append(hdr, (void *)&arg);
	if (ret != 0)
		goto err_put_jfae;

	uobj_put_read(jfc_uobj);
	uobj_alloc_commit(&jfr_uobj->uobj);
	return ret;

err_put_jfae:
	uburma_put_jfae(file);
err_delete_jfr:
	(void)ubcore_delete_jfr(jfr);
err_put_jfc:
	uobj_put_read(jfc_uobj);
err_alloc_abort:
	uobj_alloc_abort(&jfr_uobj->uobj);
	return ret;
}

static int uburma_cmd_modify_jfr(struct ubcore_device *ubc_dev,
				 struct uburma_file *file,
				 struct uburma_cmd_hdr *hdr)
{
	struct uburma_cmd_modify_jfr arg;
	struct uburma_uobj *uobj;
	struct ubcore_jfr_attr attr = { 0 };
	struct ubcore_udata udata;
	struct ubcore_jfr *jfr;
	int ret;

	ret = uburma_tlv_parse(hdr, (void *)&arg);
	if (ret != 0)
		return ret;

	attr.mask = arg.in.mask;
	attr.rx_threshold = arg.in.rx_threshold;
	attr.state = (enum ubcore_jfr_state)arg.in.state;
	fill_udata(&udata, file->ucontext, &arg.udata);

	uobj = uobj_get_write(UOBJ_CLASS_JFR, arg.in.handle, file);
	if (IS_ERR_OR_NULL(uobj)) {
		uburma_log_err("failed to find jfr.\n");
		return -EINVAL;
	}

	jfr = (struct ubcore_jfr *)uobj->object;
	ret = ubcore_modify_jfr(jfr, &attr, &udata);
	if (ret != 0) {
		uobj_put_write(uobj);
		uburma_log_err("modify jfr failed, ret:%d.\n", ret);
		return ret;
	}

	ret = uburma_tlv_append(hdr, (void *)&arg);
	uobj_put_write(uobj);
	return ret;
}

static int uburma_cmd_query_jfr(struct ubcore_device *ubc_dev,
				struct uburma_file *file,
				struct uburma_cmd_hdr *hdr)
{
	struct uburma_cmd_query_jfr arg;
	struct ubcore_jfr_attr attr = { 0 };
	struct ubcore_jfr_cfg cfg = { 0 };
	struct uburma_uobj *uobj;
	struct ubcore_jfr *jfr;
	int ret;

	ret = uburma_tlv_parse(hdr, (void *)&arg);
	if (ret != 0)
		return ret;

	uobj = uobj_get_read(UOBJ_CLASS_JFR, arg.in.handle, file);
	if (IS_ERR_OR_NULL(uobj)) {
		uburma_log_err("failed to find jfr.\n");
		return -EINVAL;
	}

	jfr = (struct ubcore_jfr *)uobj->object;
	ret = ubcore_query_jfr(jfr, &cfg, &attr);
	if (ret != 0) {
		uobj_put_read(uobj);
		uburma_log_err("query jfr failed, ret:%d.\n", ret);
		return ret;
	}

	arg.out.depth = cfg.depth;
	arg.out.flag = cfg.flag.value;
	arg.out.trans_mode = (uint32_t)cfg.trans_mode;
	arg.out.max_sge = cfg.max_sge;
	arg.out.min_rnr_timer = cfg.min_rnr_timer;
	arg.out.token = cfg.token_value.token;
	arg.out.id = cfg.id;

	arg.out.rx_threshold = attr.rx_threshold;
	arg.out.state = (uint32_t)attr.state;

	ret = uburma_tlv_append(hdr, (void *)&arg);
	uobj_put_read(uobj);
	return ret;
}

static int uburma_cmd_delete_jfr(struct ubcore_device *ubc_dev,
				 struct uburma_file *file,
				 struct uburma_cmd_hdr *hdr)
{
	struct uburma_cmd_delete_jfr arg;
	struct uburma_jfr_uobj *jfr_uobj;
	struct uburma_uobj *uobj;
	int ret;

	ret = uburma_tlv_parse(hdr, (void *)&arg);
	if (ret != 0)
		return ret;

	uobj = uobj_get_del(UOBJ_CLASS_JFR, arg.in.handle, file);
	if (IS_ERR_OR_NULL(uobj)) {
		uburma_log_err("failed to find jfr");
		return -EINVAL;
	}

	/* To get async_events_reported after obj removed. */
	uobj_get(uobj);
	jfr_uobj = container_of(uobj, struct uburma_jfr_uobj, uobj);

	ret = uobj_remove_commit(uobj);
	if (ret != 0) {
		uburma_log_err("delete jfr failed, ret:%d.\n", ret);
		uobj_put(uobj);
		uobj_put_del(uobj);
		return ret;
	}

	arg.out.async_events_reported = jfr_uobj->async_events_reported;
	uobj_put(uobj);
	uobj_put_del(uobj);
	return uburma_tlv_append(hdr, (void *)&arg);
}

static int uburma_cmd_delete_jfr_batch(struct ubcore_device *ubc_dev,
				       struct uburma_file *file,
				       struct uburma_cmd_hdr *hdr)
{
	struct uburma_jfr_uobj *jfr_uobj = NULL;
	struct uburma_cmd_delete_jfr_batch arg;
	struct uburma_uobj **uobj_arr = NULL;
	uint32_t async_events_reported = 0;
	struct uburma_uobj *uobj = NULL;
	uint32_t bad_jfr_index = 0;
	uint64_t *jfr_arr = NULL;
	uint32_t arr_num;
	int ret_bad;
	int ret;
	int i;

	ret = uburma_tlv_parse(hdr, (void *)&arg);
	if (ret != 0)
		return ret;

	arr_num = arg.in.jfr_num;
	jfr_arr = kcalloc(arr_num, sizeof(uint64_t), GFP_KERNEL);
	if (!jfr_arr)
		return -ENOMEM;

	ret = uburma_copy_from_user((void *)jfr_arr,
				    (void __user *)arg.in.jfr_ptr,
				    arr_num * sizeof(uint64_t));
	if (ret != 0)
		goto free_jfr_arr;

	uobj_arr = kcalloc(arr_num, sizeof(struct uburma_uobj *), GFP_KERNEL);
	if (!uobj_arr) {
		ret = -ENOMEM;
		goto free_jfr_arr;
	}

	for (i = 0; i < arr_num; ++i) {
		uobj = uobj_get_del(UOBJ_CLASS_JFR, jfr_arr[i], file);
		uobj_arr[i] = uobj;
		if (IS_ERR(uobj)) {
			uburma_log_err("failed to find jfr, index is %d.\n", i);
			ret = -EINVAL;
			goto free_uobj_arr;
		}
		/* To get events_reported after obj removed. */
		uobj_get(uobj);
		jfr_uobj = container_of(uobj, struct uburma_jfr_uobj, uobj);
		async_events_reported += jfr_uobj->async_events_reported;
	}

	ret = uobj_remove_commit_batch(uobj_arr, arr_num, &bad_jfr_index);
	if (ret != 0)
		uburma_log_err("delete jfr failed, ret:%d.\n", ret);

	arg.out.async_events_reported = async_events_reported;
	arg.out.bad_jfr_index = bad_jfr_index;

	uobj_put_batch(uobj_arr, arr_num);
	uobj_put_del_batch(uobj_arr, arr_num);

	ret_bad = uburma_tlv_append(hdr, (void *)&arg);
	if (ret_bad != 0) {
		uburma_log_err("uburma tlv append error, ret:%d.\n", ret_bad);
		ret = ret_bad;
	}

free_uobj_arr:
	kfree(uobj_arr);
free_jfr_arr:
	kfree(jfr_arr);
	return ret;
}

static int uburma_cmd_create_jfc(struct ubcore_device *ubc_dev,
				 struct uburma_file *file,
				 struct uburma_cmd_hdr *hdr)
{
	struct uburma_cmd_create_jfc arg;
	struct uburma_jfc_uobj *jfc_uobj;
	struct uburma_jfce_uobj *jfce;
	struct ubcore_jfc_cfg cfg = { 0 };
	struct ubcore_udata udata;
	struct ubcore_jfc *jfc;
	int ret;

	ret = uburma_tlv_parse(hdr, (void *)&arg);
	if (ret != 0)
		return ret;

	cfg.depth = arg.in.depth;
	cfg.flag.value = arg.in.flag;
	cfg.ceqn = arg.in.ceqn;

	/* jfce may be ERR_PTR */
	jfce = uburma_get_jfce_uobj(arg.in.jfce_fd, file);
	if (arg.in.jfce_fd >= 0 && IS_ERR_OR_NULL(jfce)) {
		uburma_log_err("Failed to get jfce.\n");
		return -EINVAL;
	}

	fill_udata(&udata, file->ucontext, &arg.udata);

	jfc_uobj = (struct uburma_jfc_uobj *)uobj_alloc(UOBJ_CLASS_JFC, file);
	if (IS_ERR_OR_NULL(jfc_uobj)) {
		uburma_log_err("UOBJ_CLASS_JFC alloc fail!\n");
		ret = -1;
		goto err_put_jfce;
	}
	jfc_uobj->comp_events_reported = 0;
	jfc_uobj->async_events_reported = 0;
	INIT_LIST_HEAD(&jfc_uobj->comp_event_list);
	INIT_LIST_HEAD(&jfc_uobj->async_event_list);
	cfg.jfc_context = jfc_uobj;

	jfc = ubcore_create_jfc(ubc_dev, &cfg, uburma_jfce_handler,
				uburma_jfc_event_cb, &udata);
	if (IS_ERR_OR_NULL(jfc)) {
		uburma_log_err("create jfc or get jfc_id failed.\n");
		ret = PTR_ERR(jfc);
		goto err_alloc_abort;
	}

	jfc_uobj->jfce = (struct uburma_uobj *)jfce;
	jfc_uobj->uobj.object = jfc;
	jfc->urma_jfc = arg.in.urma_jfc;

	/* Do not release jfae fd until jfc is destroyed */
	ret = uburma_get_jfae(file);
	if (ret != 0)
		goto err_delete_jfc;

	arg.out.id = jfc->id;
	arg.out.depth = jfc->jfc_cfg.depth;
	arg.out.handle = jfc_uobj->uobj.id;
	ret = uburma_tlv_append(hdr, (void *)&arg);
	if (ret != 0)
		goto err_put_jfae;

	uobj_alloc_commit(&jfc_uobj->uobj);
	return 0;

err_put_jfae:
	uburma_put_jfae(file);
err_delete_jfc:
	(void)ubcore_delete_jfc(jfc);
err_alloc_abort:
	uobj_alloc_abort(&jfc_uobj->uobj);
err_put_jfce:
	if (!IS_ERR_OR_NULL(jfce))
		uobj_put(&jfce->uobj);
	return ret;
}

static int uburma_cmd_modify_jfc(struct ubcore_device *ubc_dev,
				 struct uburma_file *file,
				 struct uburma_cmd_hdr *hdr)
{
	struct uburma_cmd_modify_jfc arg;
	struct uburma_uobj *uobj;
	struct ubcore_jfc_attr attr = { 0 };
	struct ubcore_udata udata;
	struct ubcore_jfc *jfc;
	int ret;

	ret = uburma_tlv_parse(hdr, (void *)&arg);
	if (ret != 0)
		return ret;

	attr.mask = arg.in.mask;
	attr.moderate_count = arg.in.moderate_count;
	attr.moderate_period = arg.in.moderate_period;
	fill_udata(&udata, file->ucontext, &arg.udata);

	uobj = uobj_get_write(UOBJ_CLASS_JFC, arg.in.handle, file);
	if (IS_ERR_OR_NULL(uobj)) {
		uburma_log_err("failed to find jfc.\n");
		return -EINVAL;
	}

	jfc = (struct ubcore_jfc *)uobj->object;
	ret = ubcore_modify_jfc(jfc, &attr, &udata);
	if (ret != 0) {
		uobj_put_write(uobj);
		uburma_log_err("modify jfc failed, ret:%d.\n", ret);
		return ret;
	}

	ret = uburma_tlv_append(hdr, (void *)&arg);
	uobj_put_write(uobj);
	return ret;
}

static int uburma_cmd_delete_jfc(struct ubcore_device *ubc_dev,
				 struct uburma_file *file,
				 struct uburma_cmd_hdr *hdr)
{
	struct uburma_cmd_delete_jfc arg;
	struct uburma_uobj *uobj;
	struct uburma_jfc_uobj *jfc_uobj;
	int ret;

	ret = uburma_tlv_parse(hdr, (void *)&arg);
	if (ret != 0)
		return ret;

	uobj = uobj_get_del(UOBJ_CLASS_JFC, arg.in.handle, file);
	if (IS_ERR_OR_NULL(uobj)) {
		uburma_log_err("failed to find jfc.\n");
		return -EINVAL;
	}

	/* To get events_reported after obj removed. */
	uobj_get(uobj);
	jfc_uobj = container_of(uobj, struct uburma_jfc_uobj, uobj);

	ret = uobj_remove_commit(uobj);
	if (ret != 0) {
		uburma_log_err("delete jfc failed, ret:%d.\n", ret);
		uobj_put(uobj);
		uobj_put_del(uobj);
		return ret;
	}

	arg.out.comp_events_reported = jfc_uobj->comp_events_reported;
	arg.out.async_events_reported = jfc_uobj->async_events_reported;
	uobj_put(uobj);
	uobj_put_del(uobj);
	return uburma_tlv_append(hdr, (void *)&arg);
}

static int uburma_cmd_delete_jfc_batch(struct ubcore_device *ubc_dev,
				       struct uburma_file *file,
				       struct uburma_cmd_hdr *hdr)
{
	struct uburma_jfc_uobj *jfc_uobj = NULL;
	struct uburma_cmd_delete_jfc_batch arg;
	struct uburma_uobj **uobj_arr = NULL;
	uint32_t async_events_reported = 0;
	uint32_t comp_events_reported = 0;
	struct uburma_uobj *uobj = NULL;
	uint32_t bad_jfc_index = 0;
	uint64_t *jfc_arr = NULL;
	uint32_t arr_num;
	int ret_bad;
	int ret;
	int i;

	ret = uburma_tlv_parse(hdr, (void *)&arg);
	if (ret != 0)
		return ret;

	arr_num = arg.in.jfc_num;
	jfc_arr = kcalloc(arr_num, sizeof(uint64_t), GFP_KERNEL);
	if (!jfc_arr)
		return -ENOMEM;

	ret = uburma_copy_from_user((void *)jfc_arr,
				    (void __user *)arg.in.jfc_ptr,
				    arr_num * sizeof(uint64_t));
	if (ret != 0)
		goto free_jfc_arr;

	uobj_arr = kcalloc(arr_num, sizeof(struct uburma_uobj *), GFP_KERNEL);
	if (!uobj_arr) {
		ret = -ENOMEM;
		goto free_jfc_arr;
	}

	for (i = 0; i < arr_num; ++i) {
		uobj = uobj_get_del(UOBJ_CLASS_JFC, jfc_arr[i], file);
		uobj_arr[i] = uobj;
		if (IS_ERR(uobj)) {
			uburma_log_err("failed to find jfc, index is %d.\n", i);
			ret = -EINVAL;
			goto free_uobj_arr;
		}
		/* To get events_reported after obj removed. */
		uobj_get(uobj);
		jfc_uobj = container_of(uobj, struct uburma_jfc_uobj, uobj);
		comp_events_reported += jfc_uobj->comp_events_reported;
		async_events_reported += jfc_uobj->async_events_reported;
	}

	ret = uobj_remove_commit_batch(uobj_arr, arr_num, &bad_jfc_index);
	if (ret != 0)
		uburma_log_err("delete jfc failed, ret:%d.\n", ret);

	uobj_put_batch(uobj_arr, arr_num);
	uobj_put_del_batch(uobj_arr, arr_num);

	arg.out.comp_events_reported = comp_events_reported;
	arg.out.async_events_reported = async_events_reported;
	arg.out.bad_jfc_index = bad_jfc_index;

	ret_bad = uburma_tlv_append(hdr, (void *)&arg);
	if (ret_bad != 0) {
		uburma_log_err("uburma tlv append error, ret:%d.\n", ret_bad);
		ret = ret_bad;
	}

free_uobj_arr:
	kfree(uobj_arr);
free_jfc_arr:
	kfree(jfc_arr);
	return ret;
}

static void fill_create_jetty_attr(struct ubcore_jetty_cfg *cfg,
				   struct uburma_cmd_create_jetty *arg)
{
	cfg->id = arg->in.id;
	cfg->jfs_depth = arg->in.jfs_depth;
	cfg->jfr_depth = arg->in.jfr_depth;
	cfg->flag.bs.share_jfr = arg->in.jetty_flag &
				 0x1; // see urma_jetty_flag
	cfg->flag.bs.lock_free =
		((union ubcore_jfs_flag)arg->in.jfs_flag).bs.lock_free;
	cfg->flag.bs.error_suspend =
		((union ubcore_jfs_flag)arg->in.jfs_flag).bs.error_suspend;
	cfg->flag.bs.outorder_comp =
		((union ubcore_jfs_flag)arg->in.jfs_flag).bs.outorder_comp;
	cfg->flag.bs.order_type =
		((union ubcore_jfs_flag)arg->in.jfs_flag).bs.order_type;
	// see urma_jfs_flag

	cfg->max_send_sge = arg->in.max_send_sge;
	cfg->max_send_rsge = arg->in.max_send_rsge;
	cfg->max_recv_sge = arg->in.max_recv_sge;
	cfg->max_inline_data = arg->in.max_inline_data;
	cfg->priority = arg->in.priority;
	cfg->rnr_retry = arg->in.rnr_retry;
	cfg->err_timeout = arg->in.err_timeout;
	cfg->min_rnr_timer = arg->in.min_rnr_timer;
	cfg->trans_mode = arg->in.trans_mode;
}

static void fill_create_jetty_out(struct uburma_cmd_create_jetty *arg,
				  struct ubcore_jetty *jetty)
{
	arg->out.id = jetty->jetty_id.id;
	arg->out.jfs_depth = jetty->jetty_cfg.jfs_depth;
	arg->out.jfr_depth = jetty->jetty_cfg.jfr_depth;
	arg->out.max_send_sge = jetty->jetty_cfg.max_send_sge;
	arg->out.max_send_rsge = jetty->jetty_cfg.max_send_rsge;
	arg->out.max_recv_sge = jetty->jetty_cfg.max_recv_sge;
	arg->out.max_inline_data = jetty->jetty_cfg.max_inline_data;
}

static int uburma_cmd_create_jetty(struct ubcore_device *ubc_dev,
				   struct uburma_file *file,
				   struct uburma_cmd_hdr *hdr)
{
	struct uburma_cmd_create_jetty arg = { 0 };
	struct uburma_uobj *send_jfc_uobj = ERR_PTR(-ENOENT);
	struct uburma_uobj *recv_jfc_uobj = ERR_PTR(-ENOENT);
	struct uburma_uobj *jfr_uobj = ERR_PTR(-ENOENT);
	struct uburma_uobj *jetty_grp_uobj = ERR_PTR(-ENOENT);
	struct ubcore_jetty_cfg cfg = { 0 };
	struct uburma_jetty_uobj *jetty_uobj;
	struct ubcore_udata udata;
	struct ubcore_jetty *jetty;
	int ret = 0;

	ret = uburma_tlv_parse(hdr, &arg);
	if (ret != 0)
		return ret;

	jetty_uobj =
		(struct uburma_jetty_uobj *)uobj_alloc(UOBJ_CLASS_JETTY, file);
	if (IS_ERR_OR_NULL(jetty_uobj)) {
		uburma_log_err("UOBJ_CLASS_JETTY alloc fail!\n");
		return -ENOMEM;
	}
	jetty_uobj->async_events_reported = 0;
	INIT_LIST_HEAD(&jetty_uobj->async_event_list);
	cfg.jetty_context = jetty_uobj;

	fill_create_jetty_attr(&cfg, &arg);
	cfg.eid_index = file->ucontext->eid_index;
	send_jfc_uobj =
		uobj_get_read(UOBJ_CLASS_JFC, arg.in.send_jfc_handle, file);
	recv_jfc_uobj =
		uobj_get_read(UOBJ_CLASS_JFC, arg.in.recv_jfc_handle, file);
	if (IS_ERR_OR_NULL(send_jfc_uobj) || IS_ERR_OR_NULL(recv_jfc_uobj)) {
		uburma_log_err("failed to find send %llu or recv jfc %llu.\n",
			       arg.in.send_jfc_handle, arg.in.recv_jfc_handle);
		ret = -EINVAL;
		goto err_put;
	}
	cfg.send_jfc = send_jfc_uobj->object;
	cfg.recv_jfc = recv_jfc_uobj->object;
	if (arg.in.jfr_handle != 0) {
		jfr_uobj =
			uobj_get_read(UOBJ_CLASS_JFR, arg.in.jfr_handle, file);
		if (IS_ERR_OR_NULL(jfr_uobj)) {
			uburma_log_err("failed to find jfr, jfr_handle:%llu.\n",
				       arg.in.jfr_handle);
			ret = -EINVAL;
			goto err_put;
		}
		cfg.jfr = jfr_uobj->object;
		cfg.flag.bs.share_jfr = 1;
	}
	if (arg.in.is_jetty_grp != 0) {
		jetty_grp_uobj = uobj_get_read(UOBJ_CLASS_JETTY_GRP,
					       arg.in.jetty_grp_handle, file);
		if (IS_ERR_OR_NULL(jetty_grp_uobj)) {
			uburma_log_err(
				"failed to find jetty_grp, jetty_grp_handle:%llu.\n",
				arg.in.jetty_grp_handle);
			ret = -EINVAL;
			goto err_put;
		}
		cfg.jetty_grp =
			(struct ubcore_jetty_group *)jetty_grp_uobj->object;
	}
	cfg.token_value.token = arg.in.token;
	fill_udata(&udata, file->ucontext, &arg.udata);

	jetty = ubcore_create_jetty(ubc_dev, &cfg, uburma_jetty_event_cb,
				    &udata);
	if (IS_ERR_OR_NULL(jetty)) {
		uburma_log_err("create jetty or get jetty_id failed.\n");
		ret = PTR_ERR(jetty);
		goto err_put;
	}

	jetty_uobj->uobj.object = jetty;
	jetty->urma_jetty = arg.in.urma_jetty;
	/* Do not release jfae fd until jetty is destroyed */
	ret = uburma_get_jfae(file);
	if (ret != 0)
		goto err_delete_jetty;

	fill_create_jetty_out(&arg, jetty);
	arg.out.handle = jetty_uobj->uobj.id;

	ret = uburma_tlv_append(hdr, &arg);
	if (ret != 0)
		goto err_put_jfae;

	if (cfg.jetty_grp)
		uobj_put_read(jetty_grp_uobj);
	if (cfg.jfr)
		uobj_put_read(jfr_uobj);
	uobj_put_read(send_jfc_uobj);
	uobj_put_read(recv_jfc_uobj);
	uobj_alloc_commit(&jetty_uobj->uobj);
	return 0;

err_put_jfae:
	uburma_put_jfae(file);
err_delete_jetty:
	(void)ubcore_delete_jetty(jetty);
err_put:
	if (!IS_ERR_OR_NULL(jetty_grp_uobj))
		uobj_put_read(jetty_grp_uobj);
	if (!IS_ERR_OR_NULL(jfr_uobj))
		uobj_put_read(jfr_uobj);
	if (!IS_ERR_OR_NULL(recv_jfc_uobj))
		uobj_put_read(recv_jfc_uobj);
	if (!IS_ERR_OR_NULL(send_jfc_uobj))
		uobj_put_read(send_jfc_uobj);
	uobj_alloc_abort(&jetty_uobj->uobj);
	return ret;
}

static int uburma_cmd_modify_jetty(struct ubcore_device *ubc_dev,
				   struct uburma_file *file,
				   struct uburma_cmd_hdr *hdr)
{
	struct uburma_cmd_modify_jetty arg = { 0 };
	struct uburma_uobj *uobj;
	struct ubcore_jetty_attr attr = { 0 };
	struct ubcore_jetty *jetty;
	struct ubcore_udata udata;
	int ret;

	ret = uburma_tlv_parse(hdr, &arg);
	if (ret != 0)
		return ret;

	attr.mask = arg.in.mask;
	attr.rx_threshold = arg.in.rx_threshold;
	attr.state = (enum ubcore_jetty_state)arg.in.state;
	fill_udata(&udata, file->ucontext, &arg.udata);

	uobj = uobj_get_write(UOBJ_CLASS_JETTY, arg.in.handle, file);
	if (IS_ERR_OR_NULL(uobj)) {
		uburma_log_err("failed to find jetty.\n");
		return -EINVAL;
	}

	jetty = (struct ubcore_jetty *)uobj->object;
	ret = ubcore_modify_jetty(jetty, &attr, &udata);
	if (ret != 0) {
		uobj_put_write(uobj);
		uburma_log_err("modify jetty failed, ret:%d.\n", ret);
		return ret;
	}

	ret = uburma_tlv_append(hdr, &arg);
	uobj_put_write(uobj);
	return ret;
}

static int uburma_cmd_query_jetty(struct ubcore_device *ubc_dev,
				  struct uburma_file *file,
				  struct uburma_cmd_hdr *hdr)
{
	struct uburma_cmd_query_jetty arg = { 0 };
	struct ubcore_jetty_attr attr = { 0 };
	struct ubcore_jetty_cfg cfg = { 0 };
	struct uburma_uobj *uobj;
	struct ubcore_jetty *jetty;
	int ret;

	ret = uburma_tlv_parse(hdr, &arg);
	if (ret != 0)
		return ret;

	uobj = uobj_get_read(UOBJ_CLASS_JETTY, arg.in.handle, file);
	if (IS_ERR_OR_NULL(uobj)) {
		uburma_log_err("failed to find jetty.\n");
		return -EINVAL;
	}

	jetty = (struct ubcore_jetty *)uobj->object;
	ret = ubcore_query_jetty(jetty, &cfg, &attr);
	if (ret != 0) {
		uobj_put_read(uobj);
		uburma_log_err("query jetty failed, ret:%d.\n", ret);
		return ret;
	}

	arg.out.id = cfg.id;
	arg.out.jetty_flag = cfg.flag.value;

	arg.out.jfs_depth = cfg.jfs_depth;
	arg.out.jfs_flag = 0; // todo
	arg.out.trans_mode = (uint32_t)cfg.trans_mode;
	arg.out.priority = cfg.priority;
	arg.out.max_send_sge = cfg.max_send_sge;
	arg.out.max_send_rsge = cfg.max_send_rsge;
	arg.out.max_inline_data = cfg.max_inline_data;
	arg.out.rnr_retry = cfg.rnr_retry;
	arg.out.err_timeout = cfg.err_timeout;

	if (cfg.flag.bs.share_jfr == 1) {
		arg.out.jfr_depth = cfg.jfr_depth;
		arg.out.jfr_flag = 0; // todo
		arg.out.max_recv_sge = cfg.max_recv_sge;
		arg.out.min_rnr_timer = cfg.min_rnr_timer;
		arg.out.token = cfg.token_value.token;
		arg.out.jfr_id = 0; // todo
	}

	arg.out.rx_threshold = attr.rx_threshold;
	arg.out.state = (uint32_t)attr.state;
	ret = uburma_tlv_append(hdr, &arg);
	uobj_put_read(uobj);
	return ret;
}

static int uburma_cmd_delete_jetty(struct ubcore_device *ubc_dev,
				   struct uburma_file *file,
				   struct uburma_cmd_hdr *hdr)
{
	struct uburma_cmd_delete_jetty arg;
	struct uburma_jetty_uobj *jetty_uobj;
	struct uburma_uobj *uobj;
	int ret;

	ret = uburma_tlv_parse(hdr, &arg);
	if (ret != 0)
		return ret;

	uobj = uobj_get_del(UOBJ_CLASS_JETTY, arg.in.handle, file);
	if (IS_ERR_OR_NULL(uobj)) {
		uburma_log_err("failed to find jetty");
		return -EINVAL;
	}

	/* To get async_events_reported after obj removed. */
	uobj_get(uobj);
	jetty_uobj = container_of(uobj, struct uburma_jetty_uobj, uobj);

	ret = uobj_remove_commit(uobj);
	if (ret != 0) {
		uburma_log_err("delete jetty failed, ret:%d.\n", ret);
		uobj_put(uobj);
		uobj_put_del(uobj);
		return ret;
	}

	arg.out.async_events_reported = jetty_uobj->async_events_reported;
	uobj_put(uobj);
	uobj_put_del(uobj);
	return uburma_tlv_append(hdr, &arg);
}

static int uburma_cmd_delete_jetty_batch(struct ubcore_device *ubc_dev,
					 struct uburma_file *file,
					 struct uburma_cmd_hdr *hdr)
{
	struct uburma_jetty_uobj *jetty_uobj = NULL;
	struct uburma_cmd_delete_jetty_batch arg;
	struct uburma_uobj **uobj_arr = NULL;
	uint32_t async_events_reported = 0;
	struct uburma_uobj *uobj = NULL;
	uint32_t bad_jetty_index = 0;
	uint64_t *jetty_arr = NULL;
	uint32_t arr_num;
	int ret_bad;
	int ret;
	int i;

	ret = uburma_tlv_parse(hdr, (void *)&arg);
	if (ret != 0)
		return ret;

	arr_num = arg.in.jetty_num;
	jetty_arr = kcalloc(arr_num, sizeof(uint64_t), GFP_KERNEL);
	if (!jetty_arr)
		return -ENOMEM;

	ret = uburma_copy_from_user((void *)jetty_arr,
				    (void __user *)arg.in.jetty_ptr,
				    arr_num * sizeof(uint64_t));
	if (ret != 0)
		goto free_jetty_arr;

	uobj_arr = kcalloc(arr_num, sizeof(struct uburma_uobj *), GFP_KERNEL);
	if (!uobj_arr) {
		ret = -ENOMEM;
		goto free_jetty_arr;
	}

	for (i = 0; i < arr_num; ++i) {
		uobj = uobj_get_del(UOBJ_CLASS_JETTY, jetty_arr[i], file);
		uobj_arr[i] = uobj;
		if (IS_ERR(uobj)) {
			uburma_log_err("failed to find jetty, index is %d.\n",
				       i);
			ret = -EINVAL;
			goto free_uobj_arr;
		}
		/* To get events_reported after obj removed. */
		uobj_get(uobj);
		jetty_uobj = container_of(uobj, struct uburma_jetty_uobj, uobj);
		async_events_reported += jetty_uobj->async_events_reported;
	}

	ret = uobj_remove_commit_batch(uobj_arr, arr_num, &bad_jetty_index);
	if (ret != 0)
		uburma_log_err("delete jetty failed, ret:%d.\n", ret);

	arg.out.async_events_reported = async_events_reported;
	arg.out.bad_jetty_index = bad_jetty_index;

	uobj_put_batch(uobj_arr, arr_num);
	uobj_put_del_batch(uobj_arr, arr_num);

	ret_bad = uburma_tlv_append(hdr, (void *)&arg);
	if (ret_bad != 0) {
		uburma_log_err("uburma tlv append error, ret:%d.\n", ret_bad);
		ret = ret_bad;
	}

free_uobj_arr:
	kfree(uobj_arr);
free_jetty_arr:
	kfree(jetty_arr);
	return ret;
}

static int uburma_cmd_create_jfce(struct ubcore_device *ubc_dev,
				  struct uburma_file *file,
				  struct uburma_cmd_hdr *hdr)
{
	struct uburma_cmd_create_jfce arg;
	struct uburma_jfce_uobj *jfce;
	struct uburma_uobj *uobj;

	uobj = uobj_alloc(UOBJ_CLASS_JFCE, file);
	if (IS_ERR_OR_NULL(uobj))
		return PTR_ERR(uobj);

	jfce = container_of(uobj, struct uburma_jfce_uobj, uobj);
	uburma_init_jfe(&jfce->jfe);

	arg.out.fd = uobj->id; /* should get fd before commit uobj */
	uobj_alloc_commit(uobj);

	return uburma_tlv_append(hdr, (void *)&arg);
}


int uburma_unimport_jetty(struct uburma_file *file, bool async,
			  int tjetty_handle)
{
	struct uburma_tjetty_uobj *tjetty_uobj;
	struct uburma_uobj *uobj;
	int ret;

	uobj = uobj_get_del(UOBJ_CLASS_TARGET_JETTY, tjetty_handle, file);
	if (IS_ERR_OR_NULL(uobj)) {
		uburma_log_err("failed to find tjetty");
		return -EINVAL;
	}

	uobj_get(uobj);
	tjetty_uobj = container_of(uobj, struct uburma_tjetty_uobj, uobj);
	tjetty_uobj->should_unimport_async = async;
	ret = uobj_remove_commit(uobj);
	if (ret != 0)
		uburma_log_err("ubcore_unimport_jetty_async failed.\n");

	uobj_put(uobj);
	uobj_put_del(uobj);
	return ret;
}

static int uburma_get_jetty_tjetty_objs(struct uburma_file *file,
					uint64_t jetty_handle,
					uint64_t tjetty_handle,
					struct uburma_uobj **jetty_uobj,
					struct uburma_uobj **tjetty_uobj)
{
	*jetty_uobj = uobj_get_read(UOBJ_CLASS_JETTY, jetty_handle, file);
	if (IS_ERR_OR_NULL(*jetty_uobj)) {
		uburma_log_err("failed to find jetty with handle %llu",
			       jetty_handle);
		return -EINVAL;
	}

	*tjetty_uobj =
		uobj_get_read(UOBJ_CLASS_TARGET_JETTY, tjetty_handle, file);
	if (IS_ERR_OR_NULL(*tjetty_uobj)) {
		uobj_put_read(*jetty_uobj);
		uburma_log_err("failed to find target jetty with handle %llu",
			       tjetty_handle);
		return -EINVAL;
	}
	return 0;
}

static inline void uburma_put_jetty_tjetty_objs(struct uburma_uobj *jetty_uobj,
						struct uburma_uobj *tjetty_uobj)
{
	uobj_put_read(jetty_uobj);
	uobj_put_read(tjetty_uobj);
}

int uburma_unbind_jetty(struct uburma_file *file, bool async, int jetty_handle,
			int tjetty_handle)
{
	struct uburma_tjetty_uobj *uburma_tjetty;
	struct uburma_uobj *tjetty_uobj;
	struct uburma_uobj *jetty_uobj;
	int ret;

	if (uburma_get_jetty_tjetty_objs(file, jetty_handle, tjetty_handle,
					 &jetty_uobj, &tjetty_uobj))
		return -EINVAL;

	if (async)
		ret = ubcore_unbind_jetty_async(jetty_uobj->object, 0, NULL);
	else
		ret = ubcore_unbind_jetty(jetty_uobj->object);
	if (ret != 0)
		uburma_log_err("failed to unbind jetty, ret: %d.\n", ret);

	uburma_tjetty = (struct uburma_tjetty_uobj *)(tjetty_uobj);
	uburma_tjetty->jetty_uobj = NULL;

	uburma_put_jetty_tjetty_objs(jetty_uobj, tjetty_uobj);
	return ret;
}

static int uburma_cmd_create_jetty_grp(struct ubcore_device *ubc_dev,
				       struct uburma_file *file,
				       struct uburma_cmd_hdr *hdr)
{
	struct uburma_cmd_create_jetty_grp arg;
	struct uburma_jetty_grp_uobj *jetty_grp_uobj;
	struct ubcore_jetty_grp_cfg cfg = { 0 };
	struct ubcore_udata udata;
	struct ubcore_jetty_group *jetty_grp;
	int ret;

	ret = uburma_tlv_parse(hdr, &arg);
	if (ret != 0)
		return ret;

	arg.in.name[UBCORE_JETTY_GRP_MAX_NAME - 1] = '\0';
	(void)memcpy(cfg.name, arg.in.name, UBCORE_JETTY_GRP_MAX_NAME);
	cfg.name[UBCORE_JETTY_GRP_MAX_NAME - 1] = '\0';

	cfg.token_value.token = arg.in.token;
	cfg.id = arg.in.id;
	cfg.policy = (enum ubcore_jetty_grp_policy)arg.in.policy;
	cfg.flag.value = arg.in.flag;
	cfg.eid_index = file->ucontext->eid_index;
	fill_udata(&udata, file->ucontext, &arg.udata);

	jetty_grp_uobj = (struct uburma_jetty_grp_uobj *)uobj_alloc(
		UOBJ_CLASS_JETTY_GRP, file);
	if (IS_ERR_OR_NULL(jetty_grp_uobj)) {
		uburma_log_err("UOBJ_CLASS_JETTY_GRP alloc fail!\n");
		return -ENOMEM;
	}
	jetty_grp_uobj->async_events_reported = 0;
	INIT_LIST_HEAD(&jetty_grp_uobj->async_event_list);
	cfg.user_ctx = (uint64_t)jetty_grp_uobj;

	jetty_grp = ubcore_create_jetty_grp(ubc_dev, &cfg,
					    uburma_jetty_grp_event_cb, &udata);
	if (IS_ERR_OR_NULL(jetty_grp)) {
		uburma_log_err("create jetty_grp failed.\n");
		ret = PTR_ERR(jetty_grp);
		goto err_alloc_abort;
	}
	jetty_grp_uobj->uobj.object = jetty_grp;
	jetty_grp->urma_jetty_grp = arg.in.urma_jetty_grp;

	/* Do not release jfae fd until jetty_grp is destroyed */
	ret = uburma_get_jfae(file);
	if (ret != 0)
		goto err_delete_jetty_grp;

	arg.out.id = jetty_grp->jetty_grp_id.id;
	arg.out.handle = jetty_grp_uobj->uobj.id;

	ret = uburma_tlv_append(hdr, &arg);
	if (ret != 0)
		goto err_put_jfae;

	uobj_alloc_commit(&jetty_grp_uobj->uobj);
	return ret;

err_put_jfae:
	uburma_put_jfae(file);
err_delete_jetty_grp:
	(void)ubcore_delete_jetty_grp(jetty_grp);
err_alloc_abort:
	uobj_alloc_abort(&jetty_grp_uobj->uobj);
	return ret;
}

static int uburma_cmd_delete_jetty_grp(struct ubcore_device *ubc_dev,
				       struct uburma_file *file,
				       struct uburma_cmd_hdr *hdr)
{
	struct uburma_cmd_delete_jetty_grp arg;
	struct uburma_jetty_grp_uobj *jetty_grp_uobj;
	struct uburma_uobj *uobj;
	int ret;

	ret = uburma_tlv_parse(hdr, &arg);
	if (ret != 0)
		return ret;

	uobj = uobj_get_del(UOBJ_CLASS_JETTY_GRP, (int)arg.in.handle, file);
	if (IS_ERR_OR_NULL(uobj)) {
		uburma_log_err("failed to find jetty group");
		return -EINVAL;
	}

	/* To get async_events_reported after obj removed. */
	uobj_get(uobj);
	jetty_grp_uobj = container_of(uobj, struct uburma_jetty_grp_uobj, uobj);

	ret = uobj_remove_commit(uobj);
	if (ret != 0) {
		uburma_log_err("delete jfr failed, ret:%d.\n", ret);
		uobj_put(uobj);
		uobj_put_del(uobj);
		return ret;
	}

	arg.out.async_events_reported = jetty_grp_uobj->async_events_reported;
	uobj_put(uobj);
	uobj_put_del(uobj);
	return uburma_tlv_append(hdr, &arg);
}

typedef int (*uburma_cmd_handler)(struct ubcore_device *ubc_dev,
				  struct uburma_file *file,
				  struct uburma_cmd_hdr *hdr);

static uburma_cmd_handler g_uburma_cmd_handlers[] = {
	[0] = NULL,
	[UBURMA_CMD_CREATE_CTX] = uburma_cmd_create_ctx,
	[UBURMA_CMD_REGISTER_SEG] = uburma_cmd_register_seg,
	[UBURMA_CMD_UNREGISTER_SEG] = uburma_cmd_unregister_seg,
	[UBURMA_CMD_CREATE_JFR] = uburma_cmd_create_jfr,
	[UBURMA_CMD_MODIFY_JFR] = uburma_cmd_modify_jfr,
	[UBURMA_CMD_QUERY_JFR] = uburma_cmd_query_jfr,
	[UBURMA_CMD_DELETE_JFR] = uburma_cmd_delete_jfr,
	[UBURMA_CMD_CREATE_JFS] = uburma_cmd_create_jfs,
	[UBURMA_CMD_MODIFY_JFS] = uburma_cmd_modify_jfs,
	[UBURMA_CMD_QUERY_JFS] = uburma_cmd_query_jfs,
	[UBURMA_CMD_DELETE_JFS] = uburma_cmd_delete_jfs,
	[UBURMA_CMD_CREATE_JFC] = uburma_cmd_create_jfc,
	[UBURMA_CMD_MODIFY_JFC] = uburma_cmd_modify_jfc,
	[UBURMA_CMD_DELETE_JFC] = uburma_cmd_delete_jfc,
	[UBURMA_CMD_CREATE_JFCE] = uburma_cmd_create_jfce,
	[UBURMA_CMD_CREATE_JETTY] = uburma_cmd_create_jetty,
	[UBURMA_CMD_MODIFY_JETTY] = uburma_cmd_modify_jetty,
	[UBURMA_CMD_QUERY_JETTY] = uburma_cmd_query_jetty,
	[UBURMA_CMD_DELETE_JETTY] = uburma_cmd_delete_jetty,
	[UBURMA_CMD_CREATE_JETTY_GRP] = uburma_cmd_create_jetty_grp,
	[UBURMA_CMD_DESTROY_JETTY_GRP] = uburma_cmd_delete_jetty_grp,
	[UBURMA_CMD_DELETE_JFS_BATCH] = uburma_cmd_delete_jfs_batch,
	[UBURMA_CMD_DELETE_JFR_BATCH] = uburma_cmd_delete_jfr_batch,
	[UBURMA_CMD_DELETE_JFC_BATCH] = uburma_cmd_delete_jfc_batch,
	[UBURMA_CMD_DELETE_JETTY_BATCH] = uburma_cmd_delete_jetty_batch,
};

static int uburma_cmd_parse(struct ubcore_device *ubc_dev,
			    struct uburma_file *file,
			    struct uburma_cmd_hdr *hdr)
{
	if (hdr->command < UBURMA_CMD_CREATE_CTX ||
	    hdr->command >= UBURMA_CMD_MAX ||
	    g_uburma_cmd_handlers[hdr->command] == NULL) {
		uburma_log_err("bad uburma command: %d.\n", (int)hdr->command);
		return -EINVAL;
	}
	return g_uburma_cmd_handlers[hdr->command](ubc_dev, file, hdr);
}

static inline bool is_cmd_ucontext_free(struct uburma_cmd_hdr *hdr)
{
	return hdr->command == UBURMA_CMD_CREATE_CTX;
}

long uburma_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct uburma_cmd_hdr *user_hdr = (struct uburma_cmd_hdr *)arg;
	struct uburma_device *ubu_dev;
	struct ubcore_device *ubc_dev;
	struct uburma_cmd_hdr hdr;
	struct uburma_file *file;
	int srcu_idx;
	long ret;

	if (!filp || !filp->private_data) {
		uburma_log_err("invalid param");
		return -EINVAL;
	}
	file = filp->private_data;
	ubu_dev = file->ubu_dev;
	if (!ubu_dev) {
		uburma_log_err("invalid param");
		return -EINVAL;
	}
	uburma_cmd_inc(ubu_dev);
	srcu_idx = srcu_read_lock(&ubu_dev->ubc_dev_srcu);
	ubc_dev = srcu_dereference(ubu_dev->ubc_dev, &ubu_dev->ubc_dev_srcu);
	if (!ubc_dev) {
		uburma_log_err("can not find ubcore device.\n");
		ret = -EIO;
		goto srcu_unlock;
	}

	if (cmd == UBURMA_CMD) {
		ret = (long)copy_from_user(&hdr, user_hdr,
					   sizeof(struct uburma_cmd_hdr));
		if ((ret != 0) || (hdr.args_len > UBURMA_CMD_MAX_ARGS_SIZE) ||
		    (hdr.args_len == 0 || hdr.args_addr == 0)) {
			uburma_log_err(
				"invalid input, hdr.command: %d, ret:%ld, hdr.args_len: %d\n",
				hdr.command, ret, hdr.args_len);
			ret = -EINVAL;
		} else {
			if (!is_cmd_ucontext_free(&hdr)) {
				/* Check ucontext */
				down_read(&file->ucontext_rwsem);
				if (!file->ucontext)
					ret = -EINVAL;
				else
					ret = (long)uburma_cmd_parse(
						ubc_dev, file, &hdr);
				up_read(&file->ucontext_rwsem);
			} else {
				ret = (long)uburma_cmd_parse(ubc_dev, file,
							     &hdr);
			}
		}
	} else {
		uburma_log_err("bad ioctl command.\n");
		ret = -ENOIOCTLCMD;
	}

srcu_unlock:
	srcu_read_unlock(&ubu_dev->ubc_dev_srcu, srcu_idx);
	uburma_cmd_dec(ubu_dev);
	return ret;
}

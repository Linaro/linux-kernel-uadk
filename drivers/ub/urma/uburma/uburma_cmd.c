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


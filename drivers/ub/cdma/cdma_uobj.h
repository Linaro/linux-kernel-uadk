/* SPDX-License-Identifier: GPL-2.0+ */
/* Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved. */

#ifndef __CDMA_UOBJ_H__
#define __CDMA_UOBJ_H__
#include "cdma_types.h"

enum UOBJ_TYPE {
	UOBJ_TYPE_JFCE,
	UOBJ_TYPE_JFC,
	UOBJ_TYPE_CTP,
	UOBJ_TYPE_JFS,
	UOBJ_TYPE_QUEUE,
	UOBJ_TYPE_SEGMENT
};

struct cdma_uobj {
	struct cdma_file *cfile;
	enum UOBJ_TYPE type;
	int id;
	void *object;
	atomic_t rcnt;
};

void cdma_init_uobj_idr(struct cdma_file *cfile);
struct cdma_uobj *cdma_uobj_create(struct cdma_file *cfile,
				   enum UOBJ_TYPE obj_type);
void cdma_uobj_delete(struct cdma_uobj *uobj);
struct cdma_uobj *cdma_uobj_get(struct cdma_file *cfile, int id,
				enum UOBJ_TYPE type);
void cdma_cleanup_context_uobj(struct cdma_file *cfile);
void cdma_close_uobj_fd(struct cdma_file *cfile);

#endif

/* SPDX-License-Identifier: GPL-2.0+ */
/* Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved. */

#ifndef __CDMA_MBOX_H__
#define __CDMA_MBOX_H__

#include "cdma.h"
#include <ub/ubase/ubase_comm_mbx.h>

enum {
	/* JFC CMDS */
	CDMA_CMD_WRITE_JFC_CONTEXT_VA	= 0x20,
	CDMA_CMD_READ_JFC_CONTEXT_VA	= 0x21,
	CDMA_CMD_DESTROY_JFC_CONTEXT_VA	= 0x22,
	CDMA_CMD_CREATE_JFC_CONTEXT	= 0x24,
	CDMA_CMD_MODIFY_JFC_CONTEXT	= 0x25,
	CDMA_CMD_QUERY_JFC_CONTEXT	= 0x26,
	CDMA_CMD_DESTROY_JFC_CONTEXT	= 0x27,
};

/* The mailbox operation is as follows: */
static inline void cdma_fill_mbx_attr(struct ubase_mbx_attr *attr, u32 tag,
				      u8 op, u8 mbx_ue_id)
{
	ubase_fill_mbx_attr(attr, tag, op, mbx_ue_id);
}

static inline struct ubase_cmd_mailbox *cdma_alloc_cmd_mailbox(struct cdma_dev *cdev)
{
	return ubase_alloc_cmd_mailbox(cdev->adev);
}

static inline void cdma_free_cmd_mailbox(struct cdma_dev *cdev,
					 struct ubase_cmd_mailbox *mailbox)
{
	ubase_free_cmd_mailbox(cdev->adev, mailbox);
}

int cdma_post_mailbox_ctx(struct cdma_dev *cdev, void *ctx, u32 size,
			  struct ubase_mbx_attr *attr);
struct ubase_cmd_mailbox *cdma_mailbox_query_ctx(struct cdma_dev *cdev,
						 struct ubase_mbx_attr *attr);

#endif /* CDMA_MBOX_H */

/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved.
 *
 */

#ifndef __UBASE_CMD_H__
#define __UBASE_CMD_H__

#include <ub/ubase/ubase_comm_cmd.h>

#include "ubase_dev.h"

#define UBASE_CMDQ_DESC_NUM_S		3
#define UBASE_CMDQ_DESC_NUM		1024
#define UBASE_CMDQ_TX_TIMEOUT		3000000
#define UBASE_CMDQ_MBX_TX_TIMEOUT	30000
#define UBASE_CMDQ_CLEAR_WAIT_TIME	200
#define UBASE_CMDQ_WAIT_TIME		10

#define UBASE_CMD_FLAG_IN		BIT(0)
#define UBASE_CMD_FLAG_OUT		BIT(1)
#define UBASE_CMD_FLAG_NEXT		BIT(2)
#define UBASE_CMD_FLAG_WR		BIT(3)
#define UBASE_CMD_FLAG_NO_INTR		BIT(4)
#define UBASE_CMD_FLAG_ERR_INTR		BIT(5)
#define UBASE_CMD_FLAG_GET_BD_NUM	BIT(6)

#define UBASE_UE2UE_MSG_WAIT_TIME		3000

#define UBASE_CRQ_SCHED_TIMEOUT		(HZ / 2)

#define UBASE_CMD_HEADER_LENGTH		8
#define UBASE_CMD_DATA_LENGTH		(UBASE_DESC_DATA_LEN * sizeof(__le32))
#define UBASE_CMD_MAX_DESC_SIZE \
	(UBASE_CMDQ_DESC_NUM * sizeof(struct ubase_cmdq_desc))

#define UBASE_MOVE_CRQ_RING_PTR(crq) \
	((crq)->ci = ((crq)->ci + 1) % (crq)->desc_num)

enum ubase_cmd_state {
	UBASE_STATE_CMD_DISABLE
};

struct ubase_query_version_cmd {
	__le32 firmware;
	__le32 hardware;
	__le32 rsv;
	__le32 caps[UBASE_CAP_LEN];
};

struct ubase_query_ueid_cmd {
	__le32 ueid[UBASE_BUS_EID_LEN];
	u32 rsv[2];
};

static inline void __ubase_fill_inout_buf(struct ubase_cmd_buf *buf, u16 opcode,
					  bool is_read, u32 data_size, void *data)
{
	buf->opcode = opcode;
	buf->is_read = is_read;
	buf->data_size = data_size;
	buf->data = data;
}

int ubase_cmd_init(struct ubase_dev *udev);
void ubase_cmd_uninit(struct ubase_dev *udev);

static inline void __ubase_cmd_enable(struct ubase_dev *udev)
{
	clear_bit(UBASE_STATE_CMD_DISABLE, &udev->hw.state);
}

static inline void __ubase_cmd_disable(struct ubase_dev *udev)
{
	set_bit(UBASE_STATE_CMD_DISABLE, &udev->hw.state);
}

void ubase_cmd_disable(struct ubase_dev *udev);

void ubase_cmd_setup_basic_desc(struct ubase_cmdq_desc *desc,
				enum ubase_opcode_type opcode, bool is_read,
				u8 num);
int ubase_send_cmd(struct ubase_dev *udev,
		   struct ubase_cmdq_desc *desc, int num);

int ubase_post_mailbox_by_event(struct ubase_dev *udev,
				struct ubase_cmd_buf *in,
				struct ubase_cmd_buf *out);
int __ubase_cmd_send_in(struct ubase_dev *udev, struct ubase_cmd_buf *in);
int __ubase_cmd_send_inout(struct ubase_dev *udev, struct ubase_cmd_buf *in,
			   struct ubase_cmd_buf *out);

int ubase_cmd_mbx_event_cb(struct notifier_block *nb, unsigned long action,
			   void *data);

int __ubase_register_crq_event(struct ubase_dev *udev,
			       struct ubase_crq_event_nb *nb);
void __ubase_unregister_crq_event(struct ubase_dev *udev, u16 opcode);

void ubase_crq_service_task(struct ubase_delay_work *ubase_work);

#endif

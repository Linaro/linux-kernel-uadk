/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved.
 *
 */

#ifndef _UB_UBASE_COMM_CTRLQ_H_
#define _UB_UBASE_COMM_CTRLQ_H_

#include <linux/auxiliary_bus.h>
#include <linux/types.h>

#define UBASE_CTRLQ_MAX_DATA_SIZE	1000U

#define UBASE_CTRLQ_CSQ_BASEADDR_L_REG	0x18800
#define UBASE_CTRLQ_CSQ_BASEADDR_H_REG	0x18804
#define UBASE_CTRLQ_CSQ_DEPTH_REG	0x18808
#define UBASE_CTRLQ_CSQ_TAIL_REG	0x18810
#define UBASE_CTRLQ_CSQ_HEAD_REG	0x18814
#define UBASE_CTRLQ_CRQ_BASEADDR_L_REG	0x18818
#define UBASE_CTRLQ_CRQ_BASEADDR_H_REG	0x1881c
#define UBASE_CTRLQ_CRQ_DEPTH_REG	0x18820
#define UBASE_CTRLQ_CRQ_TAIL_REG	0x18824
#define UBASE_CTRLQ_CRQ_HEAD_REG	0x18828

enum ubase_ctrlq_ser_ver {
	UBASE_CTRLQ_SER_VER_01 = 0x01,
};

enum ubase_ctrlq_ser_type {
	UBASE_CTRLQ_SER_TYPE_TP_ACL = 0x01,
	UBASE_CTRLQ_SER_TYPE_DEV_REGISTER = 0x02,
	UBASE_CTRLQ_SER_TYPE_IP_ACL = 0x03,
	UBASE_CTRLQ_SER_TYPE_QOS = 0x04,
};

enum ubase_ctrlq_opc_type {
	UBASE_CTRLQ_OPC_CREATE_TP	= 0x11,
	UBASE_CTRLQ_OPC_DESTROY_TP	= 0x12,
	UBASE_CTRLQ_OPC_TP_FLUSH_DONE	= 0x14,
};

enum ubase_ctrlq_opc_type_qos {
	UBASE_CTRLQ_OPC_QUERY_VL	= 0x01,
	UBASE_CTRLQ_OPC_QUERY_SL	= 0x02,
};

enum ubase_ctrlq_opc_type_dev_register {
	UBASE_CTRLQ_OPC_CTRLQ_CTRL	= 0x14,
	UBASE_CTRLQ_OPC_UE_RESET_CTRL	= 0x15,
};

struct ubase_ctrlq_msg {
	enum ubase_ctrlq_ser_ver	service_ver;
	enum ubase_ctrlq_ser_type	service_type;
	u8	opcode;
	u8	need_resp : 1;
	u8	is_resp : 1;
	u8	resv : 6;
	u8	resp_ret; /* must set when the is_resp field is true. */
	u16	resp_seq; /* must set when the is_resp field is true. */
	u16	in_size;
	u16	out_size;
	void	*in;
	void	*out;
};

struct ubase_ctrlq_event_nb {
	u8	service_type;
	u8	opcode;
	void	*back;
	int	(*crq_handler)(struct auxiliary_device *adev, u8 service_ver,
			       void *data, u16 len, u16 seq);
};

int ubase_ctrlq_send_msg(struct auxiliary_device *aux_dev,
			 struct ubase_ctrlq_msg *msg);

int ubase_ctrlq_register_crq_event(struct auxiliary_device *aux_dev,
				   struct ubase_ctrlq_event_nb *nb);
void ubase_ctrlq_unregister_crq_event(struct auxiliary_device *aux_dev,
				      u8 service_type, u8 opcode);

#endif

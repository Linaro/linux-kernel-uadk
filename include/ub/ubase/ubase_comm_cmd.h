/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved.
 *
 */

#ifndef _UB_UBASE_COMM_CMD_H_
#define _UB_UBASE_COMM_CMD_H_

#include <linux/auxiliary_bus.h>
#include <linux/types.h>

#define UBASE_FW_VERSION_BYTE3_MASK	GENMASK(31, 24)
#define UBASE_FW_VERSION_BYTE2_MASK	GENMASK(23, 16)
#define UBASE_FW_VERSION_BYTE1_MASK	GENMASK(15, 8)
#define UBASE_FW_VERSION_BYTE0_MASK	GENMASK(7, 0)

#define UBASE_CSQ_BASEADDR_L_REG	0x18400
#define UBASE_CSQ_BASEADDR_H_REG	0x18404
#define UBASE_CSQ_DEPTH_REG		0x18408
#define UBASE_CSQ_TAIL_REG		0x18410
#define UBASE_CSQ_HEAD_REG		0x18414
#define UBASE_CRQ_BASEADDR_L_REG	0x18418
#define UBASE_CRQ_BASEADDR_H_REG	0x1841c
#define UBASE_CRQ_DEPTH_REG		0x18420
#define UBASE_CRQ_TAIL_REG		0x18424
#define UBASE_CRQ_HEAD_REG		0x18428

enum ubase_opcode_type {
	/* Generic commands */
	UBASE_OPC_QUERY_FW_VER		= 0x0001,
	UBASE_OPC_QUERY_CTL_INFO	= 0x0003,
	UBASE_OPC_QUERY_COMM_RSRC_PARAM	= 0x0030,
	UBASE_OPC_QUERY_BUS_EID		= 0x0047,

	/* NL commands */
	UBASE_OPC_CFG_ETS_TC_INFO	= 0x2340,
	UBASE_OPC_QUERY_ETS_TCG_INFO	= 0x2341,
	UBASE_OPC_QUERY_ETS_PORT_INFO	= 0x2342,
	UBASE_OPC_QUERY_VL_AGEING_EN	= 0x2343,

	/* TP commands */
	UBASE_OPC_TP_TIMER_VA_CONFIG	= 0x3007,
	UBASE_OPC_TP_EXTDB_VA_CONFIG	= 0x3008,
	UBASE_OPC_QUERY_CTP_VL_OFFSET	= 0x3112,

	/* TA commands */
	UBASE_OPC_TA_EXTDB_VA_CONFIG	= 0x4000,
	UBASE_OPC_TA_TIMER_VA_CONFIG	= 0x4001,
	UBASE_OPC_QUERY_OOR_CAPS	= 0x4200,
	UBASE_OPC_QUERY_TA_SL_VL_MAP	= 0x4201,
	UBASE_OPC_TA_VL_SCH_CONFIG	= 0x4202,
	UBASE_OPC_QUERY_TM_Q_INFO	= 0x4205,
	UBASE_OPC_QUERY_TM_QS_INFO	= 0x4206,
	UBASE_OPC_QUERY_TM_PRI_INFO	= 0x4207,
	UBASE_OPC_QUERY_TM_PG_INFO	= 0x4208,
	UBASE_OPC_QUERY_TM_PORT_INFO	= 0x4209,
	UBASE_OPC_QUERY_FST_FVT_RQMT	= 0x4212,

	/* PHY commands */
	UBASE_OPC_QUERY_CHIP_INFO	= 0x6201,

	/* Mailbox commands */
	UBASE_OPC_POST_MB		= 0x7000,
	UBASE_OPC_QUERY_MB_ST		= 0X7001,

	/* Software commands */
	UBASE_OPC_MUE_TO_UE		= 0xF001,
	UBASE_OPC_UE_TO_MUE		= 0xF002,
	UBASE_OPC_CFG_VPORT_BUF		= 0xF003,
	UBASE_OPC_NOTIFY_UE_RESET	= 0xF006,
	UBASE_OPC_QUERY_UE_RST_RDY	= 0xF007,
	UBASE_OPC_RESET_DONE		= 0xF008,
	UBASE_OPC_DESTROY_CTX_RESOURCE	= 0xF00D,
	UBASE_OPC_UE2UE_UBASE		= 0xF00E,
};

union ubase_mbox {
	struct {
		/* MB 0 */
		__le32 in_param_l;
		/* MB 1 */
		__le32 in_param_h;
		/* MB 2 */
		__le32 cmd : 8;
		__le32 tag : 24;
		/* MB 3 */
		__le32 seq_num : 16;
		__le32 event_en : 1;
		__le32 mbx_ue_id : 8;
		__le32 rsv : 7;
		/* MB 4 */
		__le32 status : 1;
		__le32 hw_run : 1;
		__le32 rsv1 : 30;
	};

	struct {
		__le32 query_status : 1;
		__le32 query_hw_run : 1;
		__le32 query_rsv : 30;
	};
};

struct ubase_cmd_buf {
	u16	opcode;
	bool	is_read;
	u32	data_size;
	void	*data;
};

struct ubase_crq_event_nb {
	u16 opcode;
	void *back;
	int (*crq_handler)(void *dev, void *data, u32 len);
};

static inline void ubase_fill_inout_buf(struct ubase_cmd_buf *buf, u16 opcode,
					bool is_read, u32 data_size, void *data)
{
	buf->opcode = opcode;
	buf->is_read = is_read;
	buf->data_size = data_size;
	buf->data = data;
}

int ubase_cmd_send_inout(struct auxiliary_device *aux_dev,
			 struct ubase_cmd_buf *in, struct ubase_cmd_buf *out);
int ubase_cmd_send_in(struct auxiliary_device *aux_dev,
		      struct ubase_cmd_buf *in);
int ubase_cmd_send_inout_ex(struct auxiliary_device *aux_dev,
			    struct ubase_cmd_buf *in, struct ubase_cmd_buf *out,
			    u32 time_out);
int ubase_cmd_send_in_ex(struct auxiliary_device *aux_dev,
			 struct ubase_cmd_buf *in, u32 time_out);

int ubase_cmd_get_data_size(struct auxiliary_device *aux_dev, u16 opcode,
			    u16 *data_size);

int ubase_register_crq_event(struct auxiliary_device *aux_dev,
			     struct ubase_crq_event_nb *nb);
void ubase_unregister_crq_event(struct auxiliary_device *aux_dev, u16 opcode);

#endif

// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) HiSilicon Technologies Co., Ltd. 2025. All rights reserved.
 */

#define pr_fmt(fmt)	"ubus config: " fmt

#include "ubus.h"
#include "msg.h"
#include "ubus_config.h"
#include "ubus_inner.h"

struct cfg_msg_pld_req {
	/* DW0 */
	u32 rsvd0 : 4;
	u32 byte_enable : 4;
	u32 rsvd1 : 8;
	u32 entity_idx : 16;
	/* DW1 */
	u32 req_addr;
	/* DW2 */
	u32 rsvd2;
	/* DW3 */
	u32 write_data;
};

struct cfg_msg_pld_rsp {
	/* DW0 */
	u32 read_data;
	/* DW1 */
	u32 rsvd1;
	/* DW2 */
	u32 rsvd2;
	/* DW3 */
	u32 rsvd3;
};

struct cfg_msg_req_pkt {
	struct msg_pkt_header header;
	struct cfg_msg_pld_req req_payload;
};

struct cfg_msg_rsp_pkt {
	struct msg_pkt_header header;
	struct cfg_msg_pld_rsp rsp_payload;
};

enum ub_cfg_sub_msg_code {
	UB_CFG0_READ = 0,
	UB_CFG0_WRITE = 1,
	UB_CFG1_READ = 2,
	UB_CFG1_WRITE = 3
};

#define CFG_MSG_PLD_SIZE 16

struct ub_cfg {
	u8 size;
	u8 pos_mask;
	u8 byte_mask;
};

static void ub_msg_extended_header_init(struct msg_extended_header *msgetah,
					u16 plen, u8 code)
{
	msgetah->plen = plen;
	msgetah->msg_code = msg_code(code);
	msgetah->type = msg_type(code);
	msgetah->sub_msg_code = sub_msg_code(code);
}

void ub_msg_pkt_header_init(struct msg_pkt_header *header, struct ub_entity *uent,
			    u16 plen, u8 code, bool flag)
{
	struct compact_network_header *cnth = &header->nth;
	struct ub_link_header *ulh = &header->ulh;
	struct ub_entity *ubc_uent = uent->ubc->uent;
	u32 seid = ubc_uent->eid;
	u16 scna = (u16)ubc_uent->cna;
	u32 deid = uent->eid;
	u16 dcna = (u16)uent->cna;

	if (flag) {
		dcna = scna;
		deid = seid;
	}

	ulh->cfg = UB_COMPACT_LINK_CFG;

	cnth->nth_nlp = NTH_NLP_WITH_TPH;
	cnth->scna = scna;
	cnth->dcna = dcna;

	header->ctph_nlp = CTPH_NLP_UPI_40BITS_UEID;
	header->tp_opcode = CTPH_OPCODE_NOT_CNP;
	header->pad = 0;
	header->upi = uent->ubc->cluster ? uent->upi : UB_CP_UPI;
	header->seid_h = seid_high(seid);
	header->seid_l = seid_low(seid);
	header->deid = deid;
	header->ta_opcode = TAH_OPCODE_MSG;

	ub_msg_extended_header_init(&header->msgetah, plen, code);
}
EXPORT_SYMBOL_GPL(ub_msg_pkt_header_init);

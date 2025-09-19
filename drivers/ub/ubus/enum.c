// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) HiSilicon Technologies Co., Ltd. 2025. All rights reserved.
 */

#define pr_fmt(fmt)	"ubus enum: " fmt

#include "ubus.h"
#include "msg.h"
#include "port.h"
#include "enum.h"

#define ENUM_MAX_HOPS 255

enum enum_topo_query_opcode {
	ENUM_TOPO_QUERY_RESPONSE = 0,
	ENUM_TOPO_QUERY_REQUEST
};

enum enum_na_cfg_opcode {
	ENUM_NA_CFG_RESPONSE = 0,
	ENUM_NA_CFG_PRIMARY = 2,
	ENUM_NA_CFG_PORT = 3
};

enum enum_hop_type {
	ENUM_HOP_TYPE_4BIT = 0,
	ENUM_HOP_TYPE_BYTE,
	ENUM_HOP_TYPE_WORD
};

enum enum_na_query_opcode {
	ENUM_NA_QUERY_RESPONSE = 0,
	ENUM_NA_QUERY_SWITCH,
	ENUM_NA_QUERY_DEVICE,
	ENUM_NA_QUERY_PORT,
};

struct enum_na_cfg_req {
	/* DW0~DW5 */
	struct enum_pld_scan_pdu_common common;
	/* DW6 */
	u16 rsvd;
	u16 port_idx;
	/* DW7 */
	u32 cna : 24;
	u32 rsvd1 : 8;
};
#define ENUM_NA_CFG_REQ_SIZE 32

struct enum_na_cfg_rsp {
	/* DW0~DW5 */
	struct enum_pld_scan_pdu_common common;
};
#define ENUM_NA_CFG_RSP_SIZE 24

struct enum_na_query_req {
	/* DW0~DW5 */
	struct enum_pld_scan_pdu_common common;
	/* DW6 */
	u32 port_idx : 16;
	u32 rsv : 16;
};
#define ENUM_NA_QUERY_REQ_SIZE 28

struct enum_na_query_rsp {
	/* DW0~DW5 */
	struct enum_pld_scan_pdu_common common;
	/* DW6 */
	u32 cna : 24;
	u32 rsvd : 8;
};
#define ENUM_NA_QUERY_RSP_SIZE 28

static void ub_enum_pkt_header_init(void *buf)
{
	struct enum_pkt_header *header = (struct enum_pkt_header *)buf;
	struct compact_network_header *cnth = &header->cnth;
	struct ub_link_header *ulh = &header->ulh;

	ulh->cfg = UB_COMPACT_LINK_CFG;
	cnth->nth_nlp = NTH_NLP_WITHOUT_TPH;
	cnth->mgmt = 1;
	header->upi = UB_CP_UPI;
}

/* Attention: The caller must ensure that hop_type is valid */
static size_t calc_forward_path_size(struct enum_pld_scan_header *header)
{
#define FOUR_BITS_PER_DWORD 8
	u8 hop_bits[] = { SZ_4, SZ_8, SZ_16 };

	/* Path size is hops * hop_bits[], then align it to 4byte */
	return ALIGN(hop_bits[header->bits.hop_type] * header->bits.hops /
		     hop_bits[0], FOUR_BITS_PER_DWORD) / SZ_2;
}

static void set_hop_path(u8 *path, int type, u32 index, u32 val)
{
	u32 offset, mask;
	u16 *p;

	switch (type) {
	case ENUM_HOP_TYPE_4BIT:
		offset = index / SZ_2;
		mask = 0x0F << ((index % SZ_2) * SZ_4);
		val = val << ((index % SZ_2) * SZ_4);
		break;
	case ENUM_HOP_TYPE_BYTE:
		offset = index;
		mask = GENMASK(7, 0);
		break;
	case ENUM_HOP_TYPE_WORD:
		offset = index * SZ_2;
		mask = GENMASK(15, 0);
		break;
	default:
		return;
	}

	p = (u16 *)(path + offset);
	*p = (*p & ~mask) | (val & mask);
}

static void ub_enum_pld_header_init(struct ub_entity *uent, void *buf)
{
	struct enum_pld_scan_header *header;
	struct ub_entity *parent, *target;
	struct ub_port *port;
	u8 *path, *r_path;
	size_t size;
	int i, j;

	header = (struct enum_pld_scan_header *)(buf + ENUM_PKT_HEADER_SIZE);
	header->bits.hop_type = ENUM_HOP_TYPE_BYTE;
	header->bits.hops = (u32)uent->topo_rank;
	header->bits.step = 0;

	if (header->bits.hops == 0) /* First hop doesn't need path */
		return;

	header->bits.r = 1; /* Use return path */

	size = calc_forward_path_size(header);

	path = header->path;
	r_path = path + size;
	target = uent;
	for (i = uent->topo_rank - 1; i >= 0; i--) {
		parent = to_ub_entity(target->dev.parent);
		for_each_uent_port(port, parent) {
			if (port->r_uent == target) {
				set_hop_path(path, header->bits.hop_type,
					     (u32)i, (u32)port->index);
				j = uent->topo_rank - 1 - i;
				set_hop_path(r_path, header->bits.hop_type,
					     (u32)j, (u32)port->r_index);
				break;
			}
		}
		target = parent;
	}
}

/* rsp should check return value, req is inside caller, don't need */
size_t calc_enum_pld_header_size(struct enum_pld_scan_header *header, bool req)
{
	size_t bytes;

	if (!req && header->bits.hop_type > ENUM_HOP_TYPE_WORD) {
		pr_err("rsp header hop_type error, type=%u\n", header->bits.hop_type);
		return 0;
	}

	bytes = calc_forward_path_size(header);

	if (req && header->bits.r)
		bytes <<= 1;

	bytes += ENUM_PLD_SCAN_HEADER_BASE_SIZE;

	return bytes;
}
EXPORT_SYMBOL_GPL(calc_enum_pld_header_size);

/* pkt header + scan header */
static size_t ub_enum_calc_header_sz(void *buf, bool req)
{
	size_t bytes;

	bytes = calc_enum_pld_header_size((struct enum_pld_scan_header *)(buf +
					  ENUM_PKT_HEADER_SIZE), req);
	if (bytes == 0)
		return 0;

	return bytes + ENUM_PKT_HEADER_SIZE;
}

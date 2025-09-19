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

static struct topo_scan_info {
#define UB_TOPO_BUF_SZ SZ_4K
	void *buf;
	struct list_head dev_list;
} topo_scan;

static int ub_enum_topo_scan_init(void)
{
	topo_scan.buf = kzalloc(UB_TOPO_BUF_SZ, GFP_KERNEL);
	if (!topo_scan.buf)
		return -ENOMEM;

	INIT_LIST_HEAD(&topo_scan.dev_list);

	return 0;
}

static void ub_enum_topo_scan_uninit(void)
{
	kfree(topo_scan.buf);
}

static inline void ub_enum_refresh_and_init_buffer_header(struct ub_entity *uent,
							  void *buf)
{
	memset(buf, 0, UB_TOPO_BUF_SZ);
	ub_enum_pkt_header_init(buf);
	ub_enum_pld_header_init(uent, buf);
}

static void ub_enum_pld_common_setup(struct enum_pld_scan_pdu_common *common,
				     u8 cmd, u8 opcode, guid_t *guid,
				     int plen)
{
	common->bits.version = UB_ENUM_MNG_VERSION;
	common->bits.cmd = cmd;
	common->bits.opcode = opcode;
	common->msn = 0;
	common->guid = *guid;
	common->pdu_len = (u8)(plen / SZ_4);
}

static int ub_enum_topo_query(struct ub_entity *uent, void *buf, u16 start_idx,
			      u16 *actual_rsp_size)
{
	struct message_device *mdev = uent->message->mdev;
	struct enum_topo_query_req *req;
	struct msg_info info = {};
	size_t header_sz;
	int ret;

	ub_enum_refresh_and_init_buffer_header(uent, buf);

	header_sz = ub_enum_calc_header_sz(buf, true);
	req = (struct enum_topo_query_req *)(buf + header_sz);
	ub_enum_pld_common_setup(&req->common, ENUM_CMD_TOPO_QUERY,
				 ENUM_TOPO_QUERY_REQUEST, &uent->guid.id,
				 ENUM_TOPO_QUERY_REQ_SIZE);

	req->common.bits.slice_id = start_idx;

	message_info_init(&info, uent, buf, buf,
			  ((header_sz + ENUM_TOPO_QUERY_REQ_SIZE) <<
			  MSG_REQ_SIZE_OFFSET) | UB_TOPO_BUF_SZ);

	ret = message_sync_enum(mdev, &info, ENUM_CMD_TOPO_QUERY);
	if (!ret)
		*actual_rsp_size = info.actual_rsp_size;

	return ret;
}

static struct enum_topo_query_rsp *
ub_enum_topo_query_and_check(struct ub_entity *uent, void *buf, u16 start_idx)
{
	struct device *dev = &uent->ubc->dev;
	struct enum_topo_query_rsp *rsp;
	u16 actual_rsp_size;
	size_t header_sz;
	int ret;

	ret = ub_enum_topo_query(uent, buf, start_idx, &actual_rsp_size);
	if (ret) {
		dev_err(dev, "enum topo query failed, ret=%d\n", ret);
		return NULL;
	}

	header_sz = ub_enum_calc_header_sz(buf, false);
	if (header_sz == 0) {
		dev_err(dev, "enum topo query rsp header sz 0\n");
		return NULL;
	}

	if (header_sz + ENUM_TOPO_QUERY_RSP_BASE_SIZE > actual_rsp_size) {
		dev_err(dev, "enum topo query rsp pkt size invalid\n");
		return NULL;
	}

	rsp = (struct enum_topo_query_rsp *)(buf + header_sz);
	ret = rsp->common.bits.status;
	if (ret) {
		dev_err(dev, "enum rsp has error, status=%d\n", ret);
		return NULL;
	}

	if (header_sz + rsp->common.pdu_len * SZ_4 != actual_rsp_size) {
		dev_err(dev, "enum topo query rsp pdu_len invalid\n");
		return NULL;
	}

	return rsp;
}

static void ub_enum_parse_port(struct ub_entity *uent,
			       struct enum_tlv_port_info *pi, u16 num)
{
	struct enum_tlv_port_info *port_info;
	struct device *dev = &uent->ubc->dev;
	struct ub_port *port;

	for (u16 i = 0; i < num; i++) {
		port_info = pi + i;
		if (port_info->local_port_idx >= uent->port_nums) {
			dev_err(dev, "local port idx %u exceeds uent port num %u\n",
				port_info->local_port_idx, uent->port_nums);
			continue;
		}

		port = uent->ports + port_info->local_port_idx;

		port->domain_boundary = port_info->bits0.b;
		port->type = port_info->bits0.t ? VIRTUAL : PHYSICAL;
		port->shareable = (bool)port_info->bits0.w;

		/* skip link down port */
		if (!port_info->bits0.s)
			continue;

		if (!is_device(uent) && !is_idev(uent) && port->domain_boundary)
			continue;

		/* neighbor info of a boundary port shouldn't be stored */
		port->r_index = port_info->remote_port_idx;
		guid_copy(&port->r_guid, &port_info->remote_guid);
	}
}

struct enum_tlv_info {
	struct enum_tlv_slice_info *si;
	struct enum_tlv_port_info *pi;
	struct enum_tlv_port_num *pn;
	struct enum_tlv_cap_info *ci;
	u16 port_nums;
};

static int ub_enum_tlv_parse(void *tlv, int tlv_len, struct enum_tlv_info *info)
{
	struct enum_tlv_common *common;
	u16 port_nums = 0;
	int left_len = tlv_len;

	while ((left_len > 0) && (left_len % SZ_4 == 0)) {
		common = (struct enum_tlv_common *)tlv;

		switch (common->type) {
		case TLV_SLICE_INFO:
			info->si = (struct enum_tlv_slice_info *)tlv;
			break;
		case TLV_PORT_NUM:
			info->pn = (struct enum_tlv_port_num *)tlv;
			break;
		case TLV_PORT_INFO:
			if (port_nums == 0)
				info->pi = (struct enum_tlv_port_info *)tlv;
			port_nums++;
			break;
		case TLV_CAP_INFO:
			info->ci = (struct enum_tlv_cap_info *)tlv;
			break;
		default:
			pr_warn("not support tlv type[%u]\n", common->type);
		}

		if (common->len == 0) {
			pr_err("enum tlv len is invalid\n");
			return -EINVAL;
		}

		left_len -= (int)common->len;
		tlv += common->len;
	}

	info->port_nums = port_nums;

	return 0;
}

/* only check guid itself here, repetition problem is not considered */
bool ub_type_valid(struct ub_entity *uent, bool is_ctl)
{
	struct device *dev = &uent->ubc->dev;
	u16 code = uent_class(uent);
	u8 type = uent_type(uent);

	if (uent->ent_type == UB_ENT_UNKNOWN) {
		dev_err(dev, "ent type unknown, type=%#x, class=%#x\n",
			type, code);
		return false;
	}

	if (is_ctl && !is_ibus_controller(uent)) {
		dev_err(dev, "ubc bad type, type=%#x, class=%#x\n",
			type, code);
		return false;
	}

	if (!is_ctl && (is_ibus_controller(uent) || is_bus_controller(uent))) {
		dev_err(dev, "peripheral bad type, type=%#x, class=%#x\n",
			type, code);
		return false;
	}

	if (uent->pool && !(is_p_device(uent) || is_p_idevice(uent))) {
		dev_err(dev, "pool bad type, type=%#x, class=%#x\n",
			type, code);
		return false;
	}

	return true;
}

void ub_entity_type_init(struct ub_entity *uent)
{
	u8 base = uent_base_code(uent);

	switch (uent_type(uent)) {
	case UB_TYPE_CONTROLLER:
		if (base == UB_BASE_CODE_BUS_CONTROLLER) {
			if (uent->class_code == UB_CLASS_BUS_CONTROLLER)
				uent->ent_type = UB_ENT_BUS_CONTROLLER;
			else
				uent->ent_type = UB_ENT_UNKNOWN;
		} else {
			if (!uent->pool)
				uent->ent_type = UB_ENT_DEVICE;
			else
				uent->ent_type = UB_ENT_P_DEVICE;
		}
		break;
	case UB_TYPE_ICONTROLLER:
		if (base == UB_BASE_CODE_BUS_CONTROLLER) {
			uent->ent_type = UB_ENT_IBUS_CONTROLLER;
		} else {
			if (!uent->pool)
				uent->ent_type = UB_ENT_IDEVICE;
			else
				uent->ent_type = UB_ENT_P_IDEVICE;
		}
		break;
	case UB_TYPE_SWITCH:
	case UB_TYPE_ISWITCH:
		uent->ent_type = UB_ENT_SWITCH;
		break;
	default:
		uent->ent_type = UB_ENT_UNKNOWN;
	}
}

#define PORT_TOTAL_NUM_MAX 256

static int ub_enum_ent(struct ub_entity *uent, void *buf)
{
	struct device *dev = &uent->ubc->dev;
	struct enum_pld_scan_pdu_common *pc;
	int tlv_len, ret, total_num_ports;
	struct enum_tlv_info info = {};
	u8 total_slice, slice_id = 0;
	void *rsp, *tlv;

	do {
		rsp = (void *)ub_enum_topo_query_and_check(uent, buf, slice_id);
		if (!rsp) {
			ret = -EBUSY;
			goto err;
		}

		pc = (struct enum_pld_scan_pdu_common *)rsp;
		tlv_len = (int)pc->pdu_len * SZ_4 - ENUM_PLD_SCAN_PDU_COMMON_SIZE;
		tlv = rsp + ENUM_PLD_SCAN_PDU_COMMON_SIZE;

		ret = ub_enum_tlv_parse(tlv, tlv_len, &info);
		if (ret)
			goto err;

		if (slice_id == 0) {
			if (!info.si || !info.pi || !info.pn || !info.ci) {
				dev_err(dev, "tlv si/pi/pn/ci NULL\n");
				goto err;
			}

			uent->class_code = info.ci->class_code;
			ub_entity_type_init(uent);
			if (!ub_type_valid(uent, !uent->topo_rank))
				goto err;

			total_slice = info.si->total_slice;
			total_num_ports = (int)info.pn->total_num_ports;
		}

		if (!uent->ports) {
			uent->port_nums = total_num_ports;
			if (uent->port_nums > PORT_TOTAL_NUM_MAX) {
				dev_err(dev, "Total num ports is over total num max(%d).\n",
					PORT_TOTAL_NUM_MAX);
				return -EINVAL;
			}

			ret = ub_ports_setup(uent);
			if (ret) {
				dev_err(dev, "enum port setup fail, ret=%d\n",
					ret);
				return ret;
			}
		}

		ub_enum_parse_port(uent, info.pi, info.port_nums);
		slice_id++;
	} while (slice_id < total_slice);

	return 0;
err:
	if (uent->ports)
		ub_ports_unset(uent);
	return ret;
}

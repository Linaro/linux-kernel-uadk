// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) HiSilicon Technologies Co., Ltd. 2025. All rights reserved.
 */

#define pr_fmt(fmt)	"ubus pool: " fmt

#include "ubus.h"
#include "ubus_controller.h"
#include "ubus_entity.h"
#include "resource.h"
#include "msg.h"
#include "enum.h"
#include "port.h"
#include "reset.h"
#include "instance.h"
#include "ubus_inner.h"
#include "pool.h"

struct pool_msg_pkt {
	struct msg_pkt_header header;
	union {
		struct entity_reg_msg_pld reg;
	};
};

#define MSG_ENTITY_REG_SIZE (MSG_PKT_HEADER_SIZE + ENTITY_REG_PLD_SIZE)

static DEFINE_SPINLOCK(ub_fad_lock);

struct ub_fad_collection {
	struct list_head list;
	struct list_head node;
};

static struct ub_fad_collection ub_fad = {
	.list = LIST_HEAD_INIT(ub_fad.list),
	.node = LIST_HEAD_INIT(ub_fad.node),
};

static int ub_fad_res_init(struct pool_fad *fad, struct ub_entity *uent)
{
	u32 source_support_map[MAX_UB_RES_NUM] = { UB_ERS0S_SUPPORT,
						   UB_ERS1S_SUPPORT,
						   UB_ERS2S_SUPPORT };
	u32 support_feature = 0;
	size_t pg_size;
	u8 sys_pg = 0;
	int ret, i;

	if (!fad->ers_valid)
		return 0;

	ret = ub_cfg_read_byte(uent, UB_SYS_PGS, &sys_pg);
	if (ret) {
		ub_err(uent, "entity reg read UB_SYS_PGS failed, ret=%d\n", ret);
		return ret;
	}
	pg_size = (sys_pg & UB_SYS_PGS_SIZE) ? SZ_64K : SZ_4K;

	ret = ub_cfg_read_dword(uent, UB_CFG1_SUPPORT_FEATURE_L, &support_feature);
	if (ret) {
		ub_err(uent, "read cfg1 feature_l failed, ret=%d\n", ret);
		return ret;
	}

	for (i = 0; i < MAX_UB_RES_NUM; i++) {
		if (!(support_feature & source_support_map[i]))
			continue;

		uent->zone[i].res.start = ubba_gen(fad->ers[i].sa_h,
						   fad->ers[i].sa_l);
		uent->zone[i].res.end = uent->zone[i].res.start +
					((u64)fad->ers[i].ss * pg_size - 1);
		uent->zone[i].res.flags = IORESOURCE_MEM;
		uent->zone[i].res.name = ub_name(uent);
		uent->zone[i].sa_used = 1;
		uent->zone[i].ubba_used = 0;
		ub_info(uent, "mmio_idx=%d, res=%pR\n", i, &uent->zone[i].res);

		ret = ub_insert_resource(uent, i);
		if (ret) {
			ub_err(uent, "mmio[%d] insert res failed, ret=%d\n", i,
			       ret);
			goto fail;
		}

		ub_info(uent, "MMIO[%d]: ubba=%dx, size=%#llx, hpa=%#llx, wr_attr=%01u, prefetchable=%01u, order_type=%01u\n",
			i, 0, ub_resource_len(uent, i),
			uent->zone[i].res.start, 0, 0, 0);
		uent->zone[i].init_succ = 1;
	}

	return 0;

fail:
	for (i -= 1; i >= 0; i--)
		if (uent->zone[i].init_succ)
			ub_entity_free_mmio_idx(uent, i);
	return ret;
}

static void ub_fad_uent_init(struct pool_fad *fad, struct ub_entity *uent)
{
	uent->pool = true;
	uent->eid = fad->base.eid[0];
	uent->user_eid = fad->base.ueid[0];
	uent->cna = fad->base.cna;
	uent->upi = fad->base.upi;
	uent->entity_idx = fad->base.entity_idx;
	uent->ubc = ub_ubc_get(fad->ubc);
	memcpy(uent->guid.dw, fad->base.guid, UB_GUID_SIZE);
	uent->pue = uent;
	uent->is_mue = 1;
}

static int ub_fad_attach(struct pool_fad *fad)
{
	struct ub_entity *uent;
	int ret;

	if (fad->attach)
		return 0;

	uent = ub_alloc_ent();
	if (!uent)
		return -ENOMEM;

	ub_fad_uent_init(fad, uent);

	ret = ub_setup_ent(uent);
	if (ret)
		goto fail;

	ret = ub_fad_res_init(fad, uent);
	WARN_ON(ret);

	ub_entity_add(uent, uent->ubc);

	fad->uent = uent;
	fad->attach = true;
	return 0;
fail:
	ub_ubc_put(uent->ubc);
	kfree(uent);
	return ret;
}

bool ub_rsp_msg_init(struct msg_pkt_header *header, u8 status, u32 plen)
{
	struct compact_network_header *cnth = &header->nth;
	u32 seid = header->deid;
	u32 deid = eid_gen(header->seid_h, header->seid_l);
	u16 dcna = cnth->scna;
	u16 scna = cnth->dcna;

	cnth->scna = scna;
	cnth->dcna = dcna;

	header->seid_h = seid_high(seid);
	header->seid_l = seid_low(seid);
	header->deid = deid;

	header->msgetah.plen = plen;
	header->msgetah.rsp_status = status;
	header->msgetah.type = MSG_RSP;

	return (scna == dcna);
}
EXPORT_SYMBOL_GPL(ub_rsp_msg_init);

static void ub_fad_para_init(struct pool_fad *fad, struct entity_reg_msg_pld *pld)
{
	struct ub_guid *guid = (struct ub_guid *)fad->base.guid;
	char buf[SZ_64] = {};

	memcpy(&fad->base, &pld->base, sizeof(struct entity_base_info));

	(void)ub_show_guid(guid, buf);

	dev_info(&fad->ubc->dev,
		 "fad_add_idx=%u, eid=%#x, cna=%#x, upi=%#x,  u_eid=%#05x, guid=%s\n",
		 fad->base.entity_idx, fad->base.eid[0], fad->base.cna,
		 fad->base.upi, fad->base.ueid[0], buf);

	if (fad->ers_valid)
		memcpy(fad->ers, pld->ers, ENTITY_RS_PLD_SIZE);
}

static int ub_entity_reg_check(struct ub_bus_controller *ubc,
			     struct entity_reg_msg_pld *pld, bool ers_valid)
{
	struct entity_rs_info *ers;
	struct device *dev = &ubc->dev;
	int i;

	if (pld->base.eid[0] == 0 || pld->base.ueid[0] == 0) {
		dev_err(dev, "entity reg eid=%#x u_eid=%#x is invalid\n",
			pld->base.eid[0], pld->base.ueid[0]);
		return -EINVAL;
	}

	if (!ers_valid)
		return 0;

	ers = (struct entity_rs_info *)pld->ers;
	for (i = 0; i < MAX_UB_RES_NUM; i++) {
		if (ers[i].ss == 0) {
			dev_err(dev, "entity reg ers[%d] size is 0\n", i);
			return -EINVAL;
		}
	}

	return 0;
}

static u8 ub_entity_reg_handle(struct ub_bus_controller *ubc,
			     struct pool_msg_pkt *pkt, bool ers_valid,
			     struct pool_fad **start_fad)
{
	struct entity_reg_msg_pld *pld = &pkt->reg;
	struct pool_fad *fad;
	struct ub_entity *uent;
	int ret;

	uent = ub_get_ent_by_eid(pld->base.eid[0]);
	if (uent) {
		ub_entity_put(uent);
		return UB_MSG_RSP_EXEC_EEXIST;
	}

	ret = ub_entity_reg_check(ubc, pld, ers_valid);
	if (ret)
		return UB_MSG_RSP_EXEC_EINVAL;

	fad = kzalloc(sizeof(*fad), GFP_KERNEL);
	if (!fad)
		return UB_MSG_RSP_EXEC_ENOMEM;

	fad->ubc = ubc;
	fad->ers_valid = ers_valid;
	ub_fad_para_init(fad, pld);

	ret = ub_fad_attach(fad);
	if (ret) {
		dev_err(&ubc->dev, "fad idx[%u] add failed\n", fad->base.entity_idx);
		kfree(fad);
		return UB_MSG_RSP_EXEC_ENOEXEC;
	}

	spin_lock(&ub_fad_lock);
	list_add_tail(&fad->node, &ub_fad.node);
	spin_unlock(&ub_fad_lock);
	*start_fad = fad;

	return UB_MSG_RSP_SUCCESS;
}

static void
ub_entity_reg_msg_handler(struct ub_bus_controller *ubc, void *msg, u16 p_len)
{
	struct pool_msg_pkt *pkt = (struct pool_msg_pkt *)msg;
	struct msg_pkt_header *header = &pkt->header;
	struct msg_info info = {};
	struct pool_fad *start_fad;
	bool local, flag;
	u32 feature = 0;
	u8 status;
	int ret;

	if (p_len != MSG_ENTITY_REG_SIZE) {
		dev_err(&ubc->dev, "entity reg len err, len=%#x\n", p_len);
		status = UB_MSG_RSP_CMD_LEN_ERR;
		goto rsp;
	}

	ret = ub_cfg_read_dword(ubc->uent, UB_CFG1_SUPPORT_FEATURE_L, &feature);
	if (ret) {
		dev_err(&ubc->dev, "entity reg feature read failed, ret=%d\n", ret);
		status = UB_MSG_RSP_EXEC_ENOEXEC;
		goto rsp;
	}

	flag = !!(feature & UB_DECODER_JURIS);
	status = ub_entity_reg_handle(ubc, pkt, flag, &start_fad);

rsp:
	local = ub_rsp_msg_init(header, status, 0);
	message_info_init(&info, local ? ubc->uent : NULL, pkt, NULL,
			  (MSG_PKT_HEADER_SIZE << MSG_REQ_SIZE_OFFSET));

	ret = message_response(ubc->mdev, &info, header->msgetah.code);
	if (ret)
		dev_err(&ubc->dev, "send pool rsp msg, ret=%d\n", ret);

	if (status == UB_MSG_RSP_SUCCESS)
		ub_start_ent(start_fad->uent);
}

static rx_msg_handler_t pool_rx_msg_handler[UB_SUB_MSG_CODE_NUM] = {
	ub_entity_reg_msg_handler,
};

void ub_pool_rx_msg_handler(struct ub_bus_controller *ubc, void *pkt, u16 len)
{
	struct msg_pkt_header *header = (struct msg_pkt_header *)pkt;
	u8 sub_msg_code = header->msgetah.sub_msg_code;
	rx_msg_handler_t handler;

	handler = pool_rx_msg_handler[sub_msg_code];
	if (handler)
		handler(ubc, pkt, len);
	else
		dev_err(&ubc->dev, "pool sub msg code not support, code=%#x\n",
			sub_msg_code);
}

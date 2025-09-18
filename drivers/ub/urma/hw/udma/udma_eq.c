// SPDX-License-Identifier: GPL-2.0+
/* Copyright(c) 2025 HiSilicon Technologies CO., Ltd. All rights reserved. */

#define dev_fmt(fmt) "UDMA: " fmt
#define pr_fmt(fmt) "UDMA: " fmt

#include <linux/notifier.h>
#include <linux/slab.h>
#include <ub/ubase/ubase_comm_cmd.h>
#include <ub/ubase/ubase_comm_ctrlq.h>
#include <ub/ubase/ubase_comm_dev.h>
#include "udma_ctrlq_tp.h"
#include "udma_dev.h"
#include "udma_cmd.h"
#include "udma_jfs.h"
#include "udma_jfr.h"
#include "udma_jfc.h"
#include "udma_jetty.h"
#include "udma_eid.h"
#include <ub/urma/udma/udma_ctl.h>
#include "udma_eq.h"

static inline int udma_ae_tp_ctrlq_msg_deal(struct udma_dev *udma_dev,
					    struct ubase_aeq_notify_info *info,
					    uint32_t queue_num)
{
	switch (info->event_type) {
	case UBASE_EVENT_TYPE_TP_FLUSH_DONE:
		return udma_ctrlq_tp_flush_done(udma_dev, queue_num);
	case UBASE_EVENT_TYPE_TP_LEVEL_ERROR:
		return udma_ctrlq_remove_single_tp(udma_dev, queue_num, TP_ERROR);
	default:
		dev_warn(udma_dev->dev, "udma get unsupported async event.\n");
		return 0;
	}
}

static void dump_ae_aux_info(struct udma_dev *dev, uint8_t event_type)
{
	struct ubcore_user_ctl_out out = {};
	struct ubcore_user_ctl_in in = {};
	struct udma_ae_info_in info_in;

	if (!dump_aux_info)
		return;

	info_in.event_type = event_type;
	in.addr = (uint64_t)&info_in;
	in.len = sizeof(struct udma_ae_info_in);
	in.opcode = UDMA_USER_CTL_QUERY_AE_AUX_INFO;

	(void)udma_query_ae_aux_info(&dev->ub_dev, NULL, &in, &out);
}

static int udma_ae_tp_level_error(struct notifier_block *nb,
				  unsigned long event, void *data)
{
	struct ubase_event_nb *ev_nb = container_of(nb, struct ubase_event_nb, nb);
	struct auxiliary_device *adev = (struct auxiliary_device *)ev_nb->back;
	struct ubase_aeq_notify_info *info = data;
	struct udma_dev *udma_dev;
	uint32_t queue_num;

	queue_num = info->aeqe->event.queue_event.num;
	udma_dev = get_udma_dev(adev);

	dev_warn(udma_dev->dev,
		 "trigger tp level ae, event type is %d, sub type is %d, queue_num is %u.\n",
		 info->event_type, info->sub_type, queue_num);

	return udma_ae_tp_ctrlq_msg_deal(udma_dev, info, queue_num);
}

static int udma_ae_jfs_check_err(struct auxiliary_device *adev, uint32_t queue_num)
{
	struct udma_dev *udma_dev = get_udma_dev(adev);
	struct ubcore_jetty *ubcore_jetty;
	struct udma_jetty_queue *udma_sq;
	struct udma_jetty *udma_jetty;
	struct ubcore_jfs *ubcore_jfs;
	struct udma_jfs *udma_jfs;
	struct ubcore_event ae;

	xa_lock(&udma_dev->jetty_table.xa);
	udma_sq = (struct udma_jetty_queue *)xa_load(&udma_dev->jetty_table.xa, queue_num);
	if (!udma_sq) {
		dev_warn(udma_dev->dev,
			 "async event for bogus queue number = %u.\n", queue_num);
		xa_unlock(&udma_dev->jetty_table.xa);
		return -EINVAL;
	}

	if (udma_sq->is_jetty) {
		udma_jetty = to_udma_jetty_from_queue(udma_sq);
		ubcore_jetty = &udma_jetty->ubcore_jetty;
		if (ubcore_jetty->jfae_handler) {
			refcount_inc(&udma_jetty->ae_refcount);
			xa_unlock(&udma_dev->jetty_table.xa);
			ae.ub_dev = ubcore_jetty->ub_dev;
			ae.element.jetty = ubcore_jetty;
			ae.event_type = UBCORE_EVENT_JETTY_ERR;
			dump_ae_aux_info(udma_dev, ae.event_type);
			ubcore_jetty->jfae_handler(&ae, ubcore_jetty->uctx);
			if (refcount_dec_and_test(&udma_jetty->ae_refcount))
				complete(&udma_jetty->ae_comp);
		} else {
			xa_unlock(&udma_dev->jetty_table.xa);
		}
	} else {
		udma_jfs = to_udma_jfs_from_queue(udma_sq);
		ubcore_jfs = &udma_jfs->ubcore_jfs;
		if (ubcore_jfs->jfae_handler) {
			refcount_inc(&udma_jfs->ae_refcount);
			xa_unlock(&udma_dev->jetty_table.xa);
			ae.ub_dev = ubcore_jfs->ub_dev;
			ae.element.jfs = ubcore_jfs;
			ae.event_type = UBCORE_EVENT_JFS_ERR;
			dump_ae_aux_info(udma_dev, ae.event_type);
			ubcore_jfs->jfae_handler(&ae, ubcore_jfs->uctx);
			if (refcount_dec_and_test(&udma_jfs->ae_refcount))
				complete(&udma_jfs->ae_comp);
		} else {
			xa_unlock(&udma_dev->jetty_table.xa);
		}
	}

	return 0;
}

static int udma_ae_jfr_check_err(struct auxiliary_device *adev, uint32_t queue_num,
				 enum ubcore_event_type ubcore_etype)
{
	struct udma_dev *udma_dev = get_udma_dev(adev);
	struct ubcore_jfr *ubcore_jfr;
	struct udma_jfr *udma_jfr;
	struct ubcore_event ae;

	xa_lock(&udma_dev->jfr_table.xa);
	udma_jfr = (struct udma_jfr *)xa_load(&udma_dev->jfr_table.xa, queue_num);
	if (!udma_jfr) {
		dev_warn(udma_dev->dev,
			 "async event for bogus jfr number = %u.\n", queue_num);
		xa_unlock(&udma_dev->jfr_table.xa);
		return -EINVAL;
	}

	ubcore_jfr = &udma_jfr->ubcore_jfr;
	if (ubcore_jfr->jfae_handler) {
		refcount_inc(&udma_jfr->ae_refcount);
		xa_unlock(&udma_dev->jfr_table.xa);
		ae.ub_dev = ubcore_jfr->ub_dev;
		ae.element.jfr = ubcore_jfr;
		ae.event_type = ubcore_etype;
		ubcore_jfr->jfae_handler(&ae, ubcore_jfr->uctx);
		if (refcount_dec_and_test(&udma_jfr->ae_refcount))
			complete(&udma_jfr->ae_comp);
	} else {
		xa_unlock(&udma_dev->jfr_table.xa);
	}

	return 0;
}

static int udma_ae_jfc_check_err(struct auxiliary_device *adev, uint32_t queue_num)
{
	struct udma_dev *udma_dev = get_udma_dev(adev);
	struct ubcore_jfc *ubcore_jfc;
	struct udma_jfc *udma_jfc;
	struct ubcore_event ae;
	unsigned long flags;

	xa_lock_irqsave(&udma_dev->jfc_table.xa, flags);
	udma_jfc = (struct udma_jfc *)xa_load(&udma_dev->jfc_table.xa, queue_num);
	if (!udma_jfc) {
		dev_warn(udma_dev->dev,
			 "async event for bogus jfc number = %u.\n", queue_num);
		xa_unlock_irqrestore(&udma_dev->jfc_table.xa, flags);
		return -EINVAL;
	}

	ubcore_jfc = &udma_jfc->base;
	if (ubcore_jfc->jfae_handler) {
		refcount_inc(&udma_jfc->event_refcount);
		xa_unlock_irqrestore(&udma_dev->jfc_table.xa, flags);
		ae.ub_dev = ubcore_jfc->ub_dev;
		ae.element.jfc = ubcore_jfc;
		ae.event_type = UBCORE_EVENT_JFC_ERR;
		dump_ae_aux_info(udma_dev, ae.event_type);
		ubcore_jfc->jfae_handler(&ae, ubcore_jfc->uctx);
		if (refcount_dec_and_test(&udma_jfc->event_refcount))
			complete(&udma_jfc->event_comp);
	} else {
		xa_unlock_irqrestore(&udma_dev->jfc_table.xa, flags);
	}

	return 0;
}

static int udma_ae_jetty_group_check_err(struct auxiliary_device *adev, uint32_t queue_num)
{
	struct udma_dev *udma_dev = get_udma_dev(adev);
	struct ubcore_jetty_group *ubcore_jetty_grp;
	struct udma_jetty_grp *udma_jetty_grp;
	struct ubcore_event ae;

	xa_lock(&udma_dev->jetty_grp_table.xa);
	udma_jetty_grp = (struct udma_jetty_grp *)xa_load(&udma_dev->jetty_grp_table.xa, queue_num);
	if (!udma_jetty_grp) {
		dev_warn(udma_dev->dev,
			 "async event for bogus jetty group number = %u.\n", queue_num);
		xa_unlock(&udma_dev->jetty_grp_table.xa);
		return -EINVAL;
	}

	ubcore_jetty_grp = &udma_jetty_grp->ubcore_jetty_grp;
	if (ubcore_jetty_grp->jfae_handler) {
		refcount_inc(&udma_jetty_grp->ae_refcount);
		xa_unlock(&udma_dev->jetty_grp_table.xa);
		ae.ub_dev = ubcore_jetty_grp->ub_dev;
		ae.element.jetty_grp = ubcore_jetty_grp;
		ae.event_type = UBCORE_EVENT_JETTY_GRP_ERR;
		ubcore_jetty_grp->jfae_handler(&ae, ubcore_jetty_grp->uctx);
		if (refcount_dec_and_test(&udma_jetty_grp->ae_refcount))
			complete(&udma_jetty_grp->ae_comp);
	} else {
		xa_unlock(&udma_dev->jetty_grp_table.xa);
	}

	return 0;
}

static int udma_ae_jetty_level_error(struct notifier_block *nb,
				     unsigned long event, void *data)
{
	struct ubase_event_nb *ev_nb = container_of(nb, struct ubase_event_nb, nb);
	struct auxiliary_device *adev = (struct auxiliary_device *)ev_nb->back;
	struct ubase_aeq_notify_info *info = data;
	uint32_t queue_num;

	queue_num = info->aeqe->event.queue_event.num;

	dev_warn(&adev->dev,
		 "trigger jetty level ae, event type is %d, sub type is %d, queue_num is %u.\n",
		 info->event_type, info->sub_type, queue_num);

	if (info->event_type == UBASE_EVENT_TYPE_JFR_LIMIT_REACHED)
		return udma_ae_jfr_check_err(adev, queue_num, UBCORE_EVENT_JFR_LIMIT_REACHED);

	switch (info->sub_type) {
	case UBASE_SUBEVENT_TYPE_JFS_CHECK_ERROR:
		return udma_ae_jfs_check_err(adev, queue_num);
	case UBASE_SUBEVENT_TYPE_JFR_CHECK_ERROR:
		return udma_ae_jfr_check_err(adev, queue_num, UBCORE_EVENT_JFR_ERR);
	case UBASE_SUBEVENT_TYPE_JFC_CHECK_ERROR:
		return udma_ae_jfc_check_err(adev, queue_num);
	case UBASE_SUBEVENT_TYPE_JETTY_GROUP_CHECK_ERROR:
		return udma_ae_jetty_group_check_err(adev, queue_num);
	default:
		dev_warn(&adev->dev,
			 "udma get unsupported async event.\n");
		return -EINVAL;
	}
}

struct ae_operation {
	uint32_t op_code;
	notifier_fn_t call;
};

static struct ae_operation udma_ae_opts[] = {
	{UBASE_EVENT_TYPE_JETTY_LEVEL_ERROR, udma_ae_jetty_level_error},
	{UBASE_EVENT_TYPE_JFR_LIMIT_REACHED, udma_ae_jetty_level_error},
	{UBASE_EVENT_TYPE_TP_LEVEL_ERROR, udma_ae_tp_level_error},
	{UBASE_EVENT_TYPE_TP_FLUSH_DONE, udma_ae_tp_level_error},
};

void udma_unregister_ae_event(struct auxiliary_device *adev)
{
	struct udma_dev *udma_dev = get_udma_dev(adev);
	int i;

	for (i = 0; i < UBASE_EVENT_TYPE_MAX; i++) {
		if (udma_dev->ae_event_addr[i]) {
			ubase_event_unregister(adev, udma_dev->ae_event_addr[i]);
			kfree(udma_dev->ae_event_addr[i]);
			udma_dev->ae_event_addr[i] = NULL;
		}
	}
}

static int
udma_event_register(struct auxiliary_device *adev, enum ubase_event_type event_type,
		    int (*call)(struct notifier_block *nb,
				unsigned long action, void *data))
{
	struct udma_dev *udma_dev = get_udma_dev(adev);
	struct ubase_event_nb *cb;
	int ret = 0;

	cb = kzalloc(sizeof(*cb), GFP_KERNEL);
	if (!cb)
		return -ENOMEM;

	cb->drv_type = UBASE_DRV_UDMA;
	cb->event_type = event_type;
	cb->back = (void *)adev;
	cb->nb.notifier_call = call;

	ret = ubase_event_register(adev, cb);
	if (ret) {
		dev_err(&adev->dev,
			"failed to register async event, event type = %u, ret = %d.\n",
			cb->event_type, ret);
		kfree(cb);
		return ret;
	}
	udma_dev->ae_event_addr[event_type] = cb;

	return 0;
}

/* thanks to drivers/infiniband/hw/erdma/erdma_eq.c */
int udma_register_ae_event(struct auxiliary_device *adev)
{
	uint32_t i, opt_num;
	int ret;

	opt_num = sizeof(udma_ae_opts) / sizeof(struct ae_operation);
	for (i = 0; i < opt_num; ++i) {
		ret = udma_event_register(adev, udma_ae_opts[i].op_code, udma_ae_opts[i].call);
		if (ret) {
			udma_unregister_ae_event(adev);
			break;
		}
	}

	return ret;
}

/* thanks to drivers/infiniband/hw/erdma/erdma_eq.c */
int udma_register_ce_event(struct auxiliary_device *adev)
{
	int ret;

	ret = ubase_comp_register(adev, udma_jfc_completion);
	if (ret)
		dev_err(&adev->dev,
			"failed to register ce event, ret: %d.\n", ret);

	return ret;
}

static inline bool udma_check_tpn_ue_idx(struct udma_ue_idx_table *tp_ue_idx_info,
					 uint8_t ue_idx)
{
	int i;

	for (i = 0; i < tp_ue_idx_info->num; i++) {
		if (tp_ue_idx_info->ue_idx[i] == ue_idx)
			return true;
	}

	return false;
}

static int udma_save_tpn_ue_idx_info(struct udma_dev *udma_dev, uint8_t ue_idx,
				     uint32_t tpn)
{
	struct udma_ue_idx_table *tp_ue_idx_info;
	int ret;

	xa_lock(&udma_dev->tpn_ue_idx_table);
	tp_ue_idx_info = xa_load(&udma_dev->tpn_ue_idx_table, tpn);
	if (tp_ue_idx_info) {
		if (tp_ue_idx_info->num >= UDMA_UE_NUM) {
			dev_err(udma_dev->dev,
				"num exceeds the maximum value.\n");
			xa_unlock(&udma_dev->tpn_ue_idx_table);

			return -EINVAL;
		}

		if (!udma_check_tpn_ue_idx(tp_ue_idx_info, ue_idx))
			tp_ue_idx_info->ue_idx[tp_ue_idx_info->num++] = ue_idx;

		xa_unlock(&udma_dev->tpn_ue_idx_table);

		return 0;
	}
	xa_unlock(&udma_dev->tpn_ue_idx_table);

	tp_ue_idx_info = kzalloc(sizeof(*tp_ue_idx_info), GFP_KERNEL);
	if (!tp_ue_idx_info)
		return -ENOMEM;

	tp_ue_idx_info->ue_idx[tp_ue_idx_info->num++] = ue_idx;
	ret = xa_err(xa_store(&udma_dev->tpn_ue_idx_table, tpn, tp_ue_idx_info,
			      GFP_KERNEL));
	if (ret) {
		dev_err(udma_dev->dev,
			"store tpn ue idx table failed, ret is %d.\n", ret);
		goto err_store_ue_id;
	}

	return ret;

err_store_ue_id:
	kfree(tp_ue_idx_info);
	return ret;
}

static void udma_delete_tpn_ue_idx_info(struct udma_dev *udma_dev, uint32_t tpn)
{
	struct udma_ue_idx_table *tp_ue_idx_info;

	xa_lock(&udma_dev->tpn_ue_idx_table);
	tp_ue_idx_info = xa_load(&udma_dev->tpn_ue_idx_table, tpn);
	if (tp_ue_idx_info) {
		tp_ue_idx_info->num--;
		if (tp_ue_idx_info->num == 0) {
			__xa_erase(&udma_dev->tpn_ue_idx_table, tpn);
			kfree(tp_ue_idx_info);
		}
	}
	xa_unlock(&udma_dev->tpn_ue_idx_table);
}

static int udma_save_tp_info(struct udma_dev *udma_dev, struct udma_ue_tp_info *info,
			     uint8_t ue_idx)
{
#define UDMA_RSP_TP_MUL 2
	uint32_t tpn;
	int ret = 0;
	int i;

	for (i = 0; i < info->tp_cnt * UDMA_RSP_TP_MUL; i++) {
		tpn = info->start_tpn + i;
		ret = udma_save_tpn_ue_idx_info(udma_dev, ue_idx, tpn);
		if (ret) {
			dev_err(udma_dev->dev, "save tpn info fail, ret = %d, tpn = %u.\n",
				ret, tpn);
			goto err_save_ue_id;
		}
	}

	return ret;

err_save_ue_id:
	for (i--; i >= 0; i--) {
		tpn = info->start_tpn + i;
		udma_delete_tpn_ue_idx_info(udma_dev, tpn);
	}

	return ret;
}

static int udma_crq_recv_req_msg(void *dev, void *data, uint32_t len)
{
	struct udma_dev *udma_dev = get_udma_dev((struct auxiliary_device *)dev);
	struct udma_ue_tp_info *info;
	struct udma_req_msg *req;

	if (len < sizeof(*req) + sizeof(*info)) {
		dev_err(udma_dev->dev, "len of crq req is too small, len = %u.\n", len);
		return -EINVAL;
	}
	req = (struct udma_req_msg *)data;

	if (req->resp_code != UDMA_CMD_NOTIFY_MUE_SAVE_TP) {
		dev_err(udma_dev->dev, "ue to mue opcode error, opcode = %u.\n",
			req->resp_code);
		return -EINVAL;
	}
	info = (struct udma_ue_tp_info *)req->req.data;

	return udma_save_tp_info(udma_dev, info, req->dst_ue_idx);
}

static void udma_activate_dev_work(struct work_struct *work)
{
	struct udma_flush_work *flush_work = container_of(work, struct udma_flush_work, work);
	struct udma_dev *udev = flush_work->udev;
	int ret;

	ret = udma_open_ue_rx(udev, true, false, false, 0);
	if (ret)
		dev_err(udev->dev, "udma open ue rx failed, ret = %d.\n", ret);

	kfree(flush_work);
}

static int udma_crq_recv_resp_msg(void *dev, void *data, uint32_t len)
{
	struct udma_dev *udma_dev = get_udma_dev((struct auxiliary_device *)dev);
	struct udma_flush_work *flush_work;
	struct udma_resp_msg *udma_resp;

	if (len < sizeof(*udma_resp)) {
		dev_err(udma_dev->dev, "len of crq resp is too small, len = %u.\n", len);
		return -EINVAL;
	}
	udma_resp = (struct udma_resp_msg *)data;
	if (udma_resp->resp_code != UDMA_CMD_NOTIFY_UE_FLUSH_DONE) {
		dev_err(udma_dev->dev, "mue to ue opcode err, opcode = %u.\n",
			udma_resp->resp_code);
		return -EINVAL;
	}

	flush_work = kzalloc(sizeof(*flush_work), GFP_ATOMIC);
	if (!flush_work)
		return -ENOMEM;

	flush_work->udev = udma_dev;
	INIT_WORK(&flush_work->work, udma_activate_dev_work);
	queue_work(udma_dev->act_workq, &flush_work->work);

	return 0;
}

static struct ubase_crq_event_nb udma_crq_opts[] = {
	{UBASE_OPC_UE_TO_MUE, NULL, udma_crq_recv_req_msg},
	{UBASE_OPC_MUE_TO_UE, NULL, udma_crq_recv_resp_msg},
};

void udma_unregister_crq_event(struct auxiliary_device *adev)
{
	struct udma_dev *udma_dev = get_udma_dev(adev);
	struct ubase_crq_event_nb *nb = NULL;
	size_t index;

	xa_for_each(&udma_dev->crq_nb_table, index, nb) {
		xa_erase(&udma_dev->crq_nb_table, index);
		ubase_unregister_crq_event(adev, nb->opcode);
		kfree(nb);
		nb = NULL;
	}
}

static int udma_register_one_crq_event(struct auxiliary_device *adev,
				       struct ubase_crq_event_nb *crq_nb,
				       uint32_t index)
{
	struct udma_dev *udma_dev = get_udma_dev(adev);
	struct ubase_crq_event_nb *nb;
	int ret;

	nb = kzalloc(sizeof(*nb), GFP_KERNEL);
	if (!nb)
		return -ENOMEM;

	nb->opcode = crq_nb->opcode;
	nb->back = adev;
	nb->crq_handler = crq_nb->crq_handler;
	ret = ubase_register_crq_event(adev, nb);
	if (ret) {
		dev_err(udma_dev->dev,
			"register crq event failed, opcode is %u, ret is %d.\n",
			nb->opcode, ret);
		goto err_register_crq_event;
	}

	ret = xa_err(xa_store(&udma_dev->crq_nb_table, index, nb, GFP_KERNEL));
	if (ret) {
		dev_err(udma_dev->dev,
			"save crq nb entry failed, opcode is %u, ret is %d.\n",
			nb->opcode, ret);
		goto err_store_crq_nb;
	}

	return ret;

err_store_crq_nb:
	ubase_unregister_crq_event(adev, nb->opcode);
err_register_crq_event:
	kfree(nb);
	return ret;
}

int udma_register_crq_event(struct auxiliary_device *adev)
{
	uint32_t opt_num = sizeof(udma_crq_opts) / sizeof(struct ubase_crq_event_nb);
	uint32_t index;
	int ret = 0;

	for (index = 0; index < opt_num; ++index) {
		ret = udma_register_one_crq_event(adev, &udma_crq_opts[index], index);
		if (ret) {
			udma_unregister_crq_event(adev);
			break;
		}
	}

	return ret;
}

static int udma_ctrlq_send_eid_update_response(struct udma_dev *udma_dev, uint16_t seq, int ret_val)
{
	struct ubase_ctrlq_msg msg = {};
	int inbuf = 0;
	int ret;

	msg.service_ver = UBASE_CTRLQ_SER_VER_01;
	msg.service_type = UBASE_CTRLQ_SER_TYPE_DEV_REGISTER;
	msg.opcode = UDMA_CTRLQ_UPDATE_SEID_INFO;
	msg.need_resp = 0;
	msg.is_resp = 1;
	msg.resp_seq = seq;
	msg.resp_ret = (uint8_t)(-ret_val);
	msg.in = (void *)&inbuf;
	msg.in_size = sizeof(inbuf);

	ret = ubase_ctrlq_send_msg(udma_dev->comdev.adev, &msg);
	if (ret)
		dev_err(udma_dev->dev, "send eid update response failed, ret = %d, ret_val = %d.\n",
			ret, ret_val);
	return ret;
}

static int udma_ctrlq_eid_update(struct auxiliary_device *adev, uint8_t service_ver,
				 void *data, uint16_t len, uint16_t seq)
{
	struct udma_ctrlq_eid_out_update eid_entry = {};
	struct udma_dev *udma_dev;
	int ret;

	if (adev == NULL || data == NULL) {
		pr_err("adev or data is NULL.\n");
		return -EINVAL;
	}

	udma_dev = get_udma_dev(adev);
	if (len < sizeof(struct udma_ctrlq_eid_out_update)) {
		dev_err(udma_dev->dev, "msg len(%u) is invalid.\n", len);
		return udma_ctrlq_send_eid_update_response(udma_dev, seq, -EINVAL);
	}
	memcpy(&eid_entry, data, sizeof(eid_entry));
	if (eid_entry.op_type != UDMA_CTRLQ_EID_ADD && eid_entry.op_type != UDMA_CTRLQ_EID_DEL) {
		dev_err(udma_dev->dev, "update eid op type(%u) is invalid.\n", eid_entry.op_type);
		return udma_ctrlq_send_eid_update_response(udma_dev, seq, -EINVAL);
	}
	if (eid_entry.eid_info.eid_idx >= SEID_TABLE_SIZE) {
		dev_err(udma_dev->dev, "update invalid eid_idx = %u.\n",
			eid_entry.eid_info.eid_idx);
		return udma_ctrlq_send_eid_update_response(udma_dev, seq, -EINVAL);
	}
	mutex_lock(&udma_dev->eid_mutex);
	if (eid_entry.op_type == UDMA_CTRLQ_EID_ADD)
		ret = udma_add_one_eid(udma_dev, &(eid_entry.eid_info));
	else
		ret = udma_del_one_eid(udma_dev, &(eid_entry.eid_info));
	if (ret)
		dev_err(udma_dev->dev, "update eid failed, op = %u, index = %u, ret = %d.\n",
			eid_entry.op_type, eid_entry.eid_info.eid_idx, ret);
	mutex_unlock(&udma_dev->eid_mutex);

	return udma_ctrlq_send_eid_update_response(udma_dev, seq, ret);
}

static int udma_ctrlq_check_tp_status(struct udma_dev *udev, void *data,
				      uint16_t len, uint32_t tp_num,
				      struct udma_ctrlq_check_tp_active_rsp_info *rsp_info)
{
	struct udma_ctrlq_check_tp_active_req_info *req_info = NULL;
	uint32_t req_info_len = 0;
	int i;

	req_info_len = sizeof(uint32_t) +
		       sizeof(struct udma_ctrlq_check_tp_active_req_data) * tp_num;
	if (len < req_info_len) {
		dev_err(udev->dev, "msg param num(%u) is invalid.\n", tp_num);
		return -EINVAL;
	}
	req_info = kzalloc(req_info_len, GFP_KERNEL);
	if (!req_info)
		return -ENOMEM;
	memcpy(req_info, data, req_info_len);

	rcu_read_lock();
	for (i = 0; i < req_info->num; i++) {
		if (find_vpid(req_info->data[i].pid_flag))
			rsp_info->data[i].result = UDMA_CTRLQ_TPID_IN_USE;
		else
			rsp_info->data[i].result = UDMA_CTRLQ_TPID_EXITED;

		rsp_info->data[i].tp_id = req_info->data[i].tp_id;
	}
	rsp_info->num = tp_num;
	rcu_read_unlock();
	kfree(req_info);

	return 0;
}

static int udma_ctrlq_check_param(struct udma_dev *udev, void *data, uint16_t len)
{
#define UDMA_CTRLQ_HDR_LEN 12
#define UDMA_CTRLQ_MAX_BB 32
#define UDMA_CTRLQ_BB_LEN 32

	if (data == NULL) {
		dev_err(udev->dev, "data is NULL.\n");
		return -EINVAL;
	}

	if ((len < UDMA_CTRLQ_BB_LEN - UDMA_CTRLQ_HDR_LEN) ||
	    len > (UDMA_CTRLQ_BB_LEN * UDMA_CTRLQ_MAX_BB - UDMA_CTRLQ_HDR_LEN)) {
		dev_err(udev->dev, "msg data len(%u) is invalid.\n", len);
		return -EINVAL;
	}

	return 0;
}

static int udma_ctrlq_check_tp_active(struct auxiliary_device *adev,
				      uint8_t service_ver, void *data,
				      uint16_t len, uint16_t seq)
{
#define UDMA_CTRLQ_CHECK_TP_OFFSET 0xFF
	struct udma_ctrlq_check_tp_active_rsp_info *rsp_info = NULL;
	struct udma_dev *udev = get_udma_dev(adev);
	struct ubase_ctrlq_msg msg = {};
	uint32_t rsp_info_len = 0;
	uint32_t tp_num = 0;
	int ret_val;
	int ret;

	ret_val = udma_ctrlq_check_param(udev, data, len);
	if (ret_val == 0) {
		tp_num = *((uint32_t *)data) & UDMA_CTRLQ_CHECK_TP_OFFSET;
		rsp_info_len = sizeof(uint32_t) +
			       sizeof(struct udma_ctrlq_check_tp_active_rsp_data) * tp_num;
		rsp_info = kzalloc(rsp_info_len, GFP_KERNEL);
		if (!rsp_info) {
			dev_err(udev->dev, "check tp mag malloc failed.\n");
			return -ENOMEM;
		}

		ret_val = udma_ctrlq_check_tp_status(udev, data, len, tp_num, rsp_info);
		if (ret_val)
			dev_err(udev->dev, "check tp status failed, ret_val(%d).\n", ret_val);
	}

	msg.service_ver = UBASE_CTRLQ_SER_VER_01;
	msg.service_type = UBASE_CTRLQ_SER_TYPE_TP_ACL;
	msg.opcode = UDMA_CMD_CTRLQ_CHECK_TP_ACTIVE;
	msg.need_resp = 0;
	msg.is_resp = 1;
	msg.in_size = (uint16_t)rsp_info_len;
	msg.in = (void *)rsp_info;
	msg.resp_seq = seq;
	msg.resp_ret = (uint8_t)(-ret_val);

	ret = ubase_ctrlq_send_msg(adev, &msg);
	if (ret) {
		kfree(rsp_info);
		dev_err(udev->dev, "send check tp active ctrlq msg failed, ret(%d).\n", ret);
		return ret;
	}
	kfree(rsp_info);

	return (ret_val) ? ret_val : 0;
}

static struct ubase_ctrlq_event_nb udma_ctrlq_opts[] = {
	{UBASE_CTRLQ_SER_TYPE_TP_ACL, UDMA_CMD_CTRLQ_CHECK_TP_ACTIVE, NULL,
	 udma_ctrlq_check_tp_active},
	{UBASE_CTRLQ_SER_TYPE_DEV_REGISTER, UDMA_CTRLQ_UPDATE_SEID_INFO, NULL,
	 udma_ctrlq_eid_update},
};

static int udma_register_one_ctrlq_event(struct auxiliary_device *adev,
					 struct ubase_ctrlq_event_nb *ctrlq_nb,
					 uint32_t index)
{
	struct udma_dev *udma_dev = get_udma_dev(adev);
	struct ubase_ctrlq_event_nb *nb;
	int ret;

	nb = kzalloc(sizeof(*nb), GFP_KERNEL);
	if (nb == NULL)
		return -ENOMEM;

	nb->service_type = ctrlq_nb->service_type;
	nb->opcode = ctrlq_nb->opcode;
	nb->back = adev;
	nb->crq_handler = ctrlq_nb->crq_handler;
	ret = ubase_ctrlq_register_crq_event(adev, nb);
	if (ret)
		dev_err(udma_dev->dev,
			"ubase register ctrlq event failed, opcode = %u, ret is %d.\n",
			nb->opcode, ret);

	kfree(nb);

	return ret;
}

void udma_unregister_ctrlq_event(struct auxiliary_device *adev)
{
	int opt_num;
	int index;

	opt_num = ARRAY_SIZE(udma_ctrlq_opts);
	for (index = 0; index < opt_num; ++index)
		ubase_ctrlq_unregister_crq_event(adev, udma_ctrlq_opts[index].service_type,
						 udma_ctrlq_opts[index].opcode);
}

int udma_register_ctrlq_event(struct auxiliary_device *adev)
{
	int opt_num;
	int index;
	int ret;

	opt_num = ARRAY_SIZE(udma_ctrlq_opts);
	for (index = 0; index < opt_num; ++index) {
		ret = udma_register_one_ctrlq_event(adev, &udma_ctrlq_opts[index], index);
		if (ret)
			goto err_register_one_ctrlq_event;
	}

	return ret;

err_register_one_ctrlq_event:
	for (index--; index >= 0; index--) {
		ubase_ctrlq_unregister_crq_event(adev,
						 udma_ctrlq_opts[index].service_type,
						 udma_ctrlq_opts[index].opcode);
	}

	return ret;
}

int udma_register_activate_workqueue(struct udma_dev *udma_dev)
{
	udma_dev->act_workq = alloc_workqueue("udma_activate_workq", WQ_UNBOUND, 0);
	if (!udma_dev->act_workq) {
		dev_err(udma_dev->dev, "failed to create activate workqueue.\n");
		return -ENOMEM;
	}

	return 0;
}

void udma_unregister_activate_workqueue(struct udma_dev *udma_dev)
{
	flush_workqueue(udma_dev->act_workq);
	destroy_workqueue(udma_dev->act_workq);
}

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
#include <ub/urma/udma/udma_ctl.h>
#include "udma_eq.h"

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

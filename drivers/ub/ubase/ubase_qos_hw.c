// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved.
 *
 */

#include "ubase_cmd.h"
#include "ubase_ctrlq.h"
#include "ubase_hw.h"

int ubase_query_sl_vl_map(struct ubase_dev *udev, u8 *sl_vl)
{
	struct ubase_config_sl_vl_cmd resp = {0};
	struct ubase_config_sl_vl_cmd req = {0};
	struct ubase_cmd_buf in, out;
	int ret;

	req.sl_num = UBASE_MAX_SL_NUM;
	__ubase_fill_inout_buf(&in, UBASE_OPC_QUERY_TA_SL_VL_MAP, true,
			       sizeof(req), &req);
	__ubase_fill_inout_buf(&out, UBASE_OPC_QUERY_TA_SL_VL_MAP, false,
			       sizeof(resp), &resp);

	ret = __ubase_cmd_send_inout(udev, &in, &out);
	if (ret) {
		ubase_err(udev, "failed to query sl_vl map, ret = %d.\n", ret);
		return ret;
	}

	memcpy(sl_vl, resp.sl_vl, UBASE_MAX_SL_NUM);
	return 0;
}

static int ubase_query_vl_ageing(struct ubase_dev *udev, u16 *vl_ageing_en)
{
	struct ubase_query_vl_ageing_cmd resp = {0};
	struct ubase_query_vl_ageing_cmd req = {0};
	struct ubase_cmd_buf in, out;
	int ret;

	ubase_fill_inout_buf(&in, UBASE_OPC_QUERY_VL_AGEING_EN, true,
			     sizeof(req), &req);
	ubase_fill_inout_buf(&out, UBASE_OPC_QUERY_VL_AGEING_EN, false,
			     sizeof(resp), &resp);

	ret = __ubase_cmd_send_inout(udev, &in, &out);
	if (ret) {
		ubase_err(udev,
			  "failed to query vl ageing configuration, ret = %d.\n",
			  ret);
		return ret;
	}

	*vl_ageing_en = le16_to_cpu(resp.vl_ageing_en);

	ubase_dbg(udev, "vl_ageing_en bitmap:%u.\n", *vl_ageing_en);

	return 0;
}

static int ubase_query_ctp_vl_offset(struct ubase_dev *udev, u8 *ctp_vl_offset)
{
	struct ubase_query_ctp_vl_offset_cmd resp = {0};
	struct ubase_query_ctp_vl_offset_cmd req = {0};
	struct ubase_cmd_buf in, out;
	int ret;

	ubase_fill_inout_buf(&in, UBASE_OPC_QUERY_CTP_VL_OFFSET, true,
			     sizeof(req), &req);
	ubase_fill_inout_buf(&out, UBASE_OPC_QUERY_CTP_VL_OFFSET, false,
			     sizeof(resp), &resp);

	ret = __ubase_cmd_send_inout(udev, &in, &out);
	if (ret) {
		ubase_err(udev,
			  "failed to query ctp vl offset, ret = %d.\n", ret);
		return ret;
	}

	*ctp_vl_offset = resp.ctp_vl_offset;

	ubase_dbg(udev, "ctp_vl_offset:%u.\n", *ctp_vl_offset);

	return 0;
}

static inline void ubase_parse_udma_req_vl_uboe(struct ubase_dev *udev)
{
	struct ubase_adev_qos *qos = &udev->qos;

	qos->tp_vl_num = qos->vl_num;
	memcpy(qos->tp_req_vl, qos->vl, qos->vl_num);
}

static int ubase_parse_udma_req_vl_ub(struct ubase_dev *udev)
{
	struct ubase_adev_qos *qos = &udev->qos;
	unsigned long vl_ageing_en;
	int ret;
	u8 i;

	ret = ubase_query_vl_ageing(udev, (u16 *)&vl_ageing_en);
	if (ret)
		return ret;

	for (i = 0; i < qos->vl_num; i++) {
		if (test_bit(qos->vl[i], &vl_ageing_en))
			qos->tp_req_vl[qos->tp_vl_num++] =
				qos->vl[i];
		else
			qos->ctp_req_vl[qos->ctp_vl_num++] =
				qos->vl[i];
	}

	return 0;
}

static int ubase_parse_udma_req_vl(struct ubase_dev *udev)
{
	if (ubase_dev_ubl_supported(udev))
		return ubase_parse_udma_req_vl_ub(udev);

	ubase_parse_udma_req_vl_uboe(udev);
	return 0;
}

static int ubase_check_ctp_resp_vl(struct ubase_dev *udev, u8 ctp_vl_offset)
{
	struct ubase_adev_qos *qos = &udev->qos;
	u8 i, ctp_resp_vl_off;

	for (i = 0; i < qos->ctp_vl_num; i++) {
		ctp_resp_vl_off = qos->ctp_req_vl[i] + ctp_vl_offset;
		if (ctp_resp_vl_off >= UBASE_MAX_VL_NUM) {
			ubase_err(udev,
				  "the %uth ctp_resp_vl(%u) exceed max_vl_num(%u).\n",
				  i, ctp_resp_vl_off, UBASE_MAX_VL_NUM);
			return -EINVAL;
		}
	}

	return 0;
}

static int ubase_parse_ctp_resp_vl(struct ubase_dev *udev)
{
	struct ubase_adev_qos *qos = &udev->qos;
	u8 ctp_vl_offset;
	int ret;

	ret = ubase_query_ctp_vl_offset(udev, &ctp_vl_offset);
	if (ret)
		return ret;

	ret = ubase_check_ctp_resp_vl(udev, ctp_vl_offset);
	if (ret)
		return ret;

	qos->ctp_resp_vl_offset = ctp_vl_offset;

	return 0;
}

static bool ubase_get_vl_sl(struct ubase_dev *udev, u8 vl, u8 *sl, u8 *sl_num)
{
	bool sl_exist = false;
	u8 i;

	for (i = 0; i < UBASE_MAX_SL_NUM; i++) {
		if (udev->qos.ue_sl_vl[i] == vl) {
			sl[(*sl_num)++] = i;
			sl_exist = true;
		}
	}

	return sl_exist;
}

static int ubase_parse_udma_tp_sl(struct ubase_dev *udev)
{
	struct ubase_adev_qos *qos = &udev->qos;
	bool exist;
	u8 i;

	for (i = 0; i < qos->tp_vl_num; i++) {
		exist = ubase_get_vl_sl(udev, qos->tp_req_vl[i],
					qos->tp_sl, &qos->tp_sl_num);
		if (!exist) {
			ubase_err(udev,
				  "udma tp req vl(%u) doesn't have a corresponding sl.\n",
				  qos->tp_req_vl[i]);
			return -EINVAL;
		}
	}

	return 0;
}

static int ubase_parse_udma_ctp_sl(struct ubase_dev *udev)
{
	struct ubase_adev_qos *qos = &udev->qos;
	bool exist;
	u8 i;

	for (i = 0; i < qos->ctp_vl_num; i++) {
		exist = ubase_get_vl_sl(udev, qos->ctp_req_vl[i],
					qos->ctp_sl, &qos->ctp_sl_num);
		if (!exist) {
			ubase_err(udev,
				  "udma ctp req vl(%u) doesn't have a corresponding sl.\n",
				  qos->ctp_req_vl[i]);
			return -EINVAL;
		}
	}

	return 0;
}

static int ubase_parse_udma_sl(struct ubase_dev *udev)
{
	int ret;

	ret = ubase_parse_udma_tp_sl(udev);
	if (ret)
		return ret;

	return ubase_parse_udma_ctp_sl(udev);
}

static void ubase_gather_udma_req_resp_vl(struct ubase_dev *udev,
					       u8 *req_vl, u8 req_vl_num,
					       u8 resp_vl_off)
{
	struct ubase_caps *dev_caps = &udev->caps.dev_caps;
	struct ubase_adev_qos *qos = &udev->qos;
	u8 i, j;

	for (i = 0; i < req_vl_num; i++) {
		for (j = 0; j < qos->nic_vl_num; j++) {
			if (req_vl[i] == dev_caps->req_vl[j]) {
				dev_caps->resp_vl[j] = req_vl[i] + resp_vl_off;
				break;
			}
		}

		if (j < qos->nic_vl_num)
			continue;

		dev_caps->req_vl[dev_caps->vl_num] = req_vl[i];
		dev_caps->resp_vl[dev_caps->vl_num] = req_vl[i] + resp_vl_off;
		dev_caps->vl_num++;
	}
}

static void ubase_gather_urma_req_resp_vl(struct ubase_dev *udev)
{
	struct ubase_caps *dev_caps = &udev->caps.dev_caps;
	struct ubase_adev_qos *qos = &udev->qos;

	memcpy(dev_caps->req_vl, qos->nic_vl, qos->nic_vl_num);
	memcpy(dev_caps->resp_vl, qos->nic_vl, qos->nic_vl_num);
	dev_caps->vl_num = qos->nic_vl_num;

	/* Restriction: The unic vl can't be used as the dma resp vl. */
	ubase_gather_udma_req_resp_vl(udev, qos->tp_req_vl,
				      qos->tp_vl_num,
				      qos->tp_resp_vl_offset);
	ubase_gather_udma_req_resp_vl(udev, qos->ctp_req_vl,
				      qos->ctp_vl_num,
				      qos->ctp_resp_vl_offset);

	/* dev_caps->vl_num is used for DCB tool configuration. Therefore,
	 * dev_caps->vl_num cannot exceed IEEE_8021QAZ_MAX_TCS.
	 */
	dev_caps->vl_num = min(dev_caps->vl_num, IEEE_8021QAZ_MAX_TCS);
}

static inline void ubase_gather_cdma_req_resp_vl(struct ubase_dev *udev)
{
	struct ubase_adev_qos *qos = &udev->qos;

	ubase_gather_udma_req_resp_vl(udev, qos->ctp_req_vl,
				      qos->ctp_vl_num,
				      qos->ctp_resp_vl_offset);
}

static inline int ubase_parse_udma_resp_vl(struct ubase_dev *udev)
{
	if (ubase_dev_ubl_supported(udev))
		return ubase_parse_ctp_resp_vl(udev);

	return 0;
}

static int ubase_assign_urma_vl(struct ubase_dev *udev, u8 *urma_sl,
				u8 urma_sl_num, u8 *urma_vl, u8 *urma_vl_num)
{
	u8 urma_vl_bitmap[UBASE_MAX_VL_NUM] = {0};
	u8 i, current_vl;
	int index = 0;

	for (i = 0; i < urma_sl_num; i++) {
		current_vl = udev->qos.ue_sl_vl[urma_sl[i]];
		if (current_vl >= UBASE_MAX_VL_NUM) {
			ubase_err(udev,
				  "urma vl(%u) exceeds the maximum(%u).\n",
				  current_vl, UBASE_MAX_VL_NUM);
			return -EINVAL;
		}

		if (urma_vl_bitmap[current_vl] == 0) {
			urma_vl[index++] = current_vl;
			urma_vl_bitmap[current_vl] = 1;
			(*urma_vl_num)++;
		}
	}

	return 0;
}

static int ubase_parse_rack_nic_vl(struct ubase_dev *udev)
{
	return ubase_assign_urma_vl(udev, udev->qos.nic_sl, udev->qos.nic_sl_num,
				    udev->qos.nic_vl, &udev->qos.nic_vl_num);
}

static int ubase_parse_rack_udma_req_vl(struct ubase_dev *udev)
{
	int ret;

	ret = ubase_assign_urma_vl(udev, udev->qos.sl,
				   udev->qos.sl_num, udev->qos.vl,
				   &udev->qos.vl_num);
	if (ret)
		return ret;

	return ubase_parse_udma_req_vl(udev);
}

static int ubase_parse_rack_udma_vl(struct ubase_dev *udev)
{
	int ret;

	ret = ubase_parse_rack_udma_req_vl(udev);
	if (ret)
		return ret;

	return ubase_parse_udma_resp_vl(udev);
}

static int ubase_parse_rack_cdma_resp_vl(struct ubase_dev *udev)
{
	return ubase_parse_ctp_resp_vl(udev);
}

static int ubase_parse_rack_cdma_req_sl_vl(struct ubase_dev *udev)
{
	struct ubase_adev_qos *qos = &udev->qos;
	bool exist = false;
	u8 i;

	for (i = 0; i < qos->vl_num; i++) {
		exist = ubase_get_vl_sl(udev, qos->vl[i], qos->ctp_sl,
					&qos->ctp_sl_num);
		if (exist)
			qos->ctp_req_vl[qos->ctp_vl_num++] = qos->vl[i];
	}

	if (!qos->ctp_vl_num) {
		ubase_err(udev, "cdma doesn't have any req vl.\n");
		return -EINVAL;
	}

	return 0;
}

static int ubase_parse_rack_cdma_sl_vl(struct ubase_dev *udev)
{
	int ret;

	ret = ubase_parse_rack_cdma_req_sl_vl(udev);
	if (ret)
		return ret;

	ret = ubase_parse_rack_cdma_resp_vl(udev);
	if (ret)
		return ret;

	ubase_gather_cdma_req_resp_vl(udev);
	return 0;
}

static inline int ubase_parse_rack_nic_sl_vl(struct ubase_dev *udev)
{
	return ubase_parse_rack_nic_vl(udev);
}

static inline int ubase_parse_rack_udma_sl_vl(struct ubase_dev *udev)
{
	int ret;

	ret = ubase_parse_rack_udma_vl(udev);
	if (ret)
		return ret;

	return ubase_parse_udma_sl(udev);
}

static int ubase_parse_rack_urma_sl_vl(struct ubase_dev *udev)
{
	int ret;

	ret = ubase_parse_rack_nic_sl_vl(udev);
	if (ret)
		return ret;

	if (ubase_dev_udma_supported(udev)) {
		ret = ubase_parse_rack_udma_sl_vl(udev);
		if (ret)
			return ret;
	}

	ubase_gather_urma_req_resp_vl(udev);
	return 0;
}

static int ubase_parse_rack_adev_sl_vl(struct ubase_dev *udev)
{
	if (ubase_dev_cdma_supported(udev))
		return ubase_parse_rack_cdma_sl_vl(udev);

	if (ubase_dev_urma_supported(udev))
		return ubase_parse_rack_urma_sl_vl(udev);

	return 0;
}

static void ubase_init_udma_dscp_vl(struct ubase_dev *udev)
{
	struct ubase_adev_qos *qos = &udev->qos;
	u8 i;

	for (i = 0; i < UBASE_MAX_DSCP; i++)
		qos->dscp_vl[i] = qos->tp_req_vl[0];
}

static int ubase_parse_sl_vl(struct ubase_dev *udev)
{
	int ret;

	ret = ubase_query_sl_vl_map(udev, udev->qos.ue_sl_vl);
	if (ret)
		return ret;

	ret = ubase_parse_rack_adev_sl_vl(udev);
	if (ret)
		return ret;

	if (ubase_dev_udma_supported(udev))
		ubase_init_udma_dscp_vl(udev);

	if (ubase_utp_supported(udev) && ubase_dev_urma_supported(udev))
		udev->caps.unic_caps.tpg.max_cnt = udev->qos.nic_vl_num;

	return 0;
}

static int ubase_ctrlq_query_vl(struct ubase_dev *udev)
{
	struct ubase_ctrlq_query_vl_resp resp = {0};
	struct ubase_ctrlq_query_vl_req req = {0};
	struct ubase_ctrlq_msg msg = {0};
	unsigned long vl_bitmap;
	u8 i, vl_cnt = 0;
	int ret;

	msg.service_ver = UBASE_CTRLQ_SER_VER_01;
	msg.service_type = UBASE_CTRLQ_SER_TYPE_QOS;
	msg.opcode = UBASE_CTRLQ_OPC_QUERY_VL;
	msg.need_resp = 1;
	msg.is_resp = 0;
	msg.in_size = sizeof(req);
	msg.in = &req;
	msg.out_size = sizeof(resp);
	msg.out = &resp;

	ret = __ubase_ctrlq_send(udev, &msg, NULL);
	if (ret) {
		ubase_err(udev,
			  "failed to send ctrlq msg when query vl, ret = %d.\n", ret);
		return ret;
	}

	vl_bitmap = le16_to_cpu(resp.vl_bitmap);

	for (i = 0; i < UBASE_MAX_VL_NUM; i++)
		if (test_bit(i, &vl_bitmap))
			udev->qos.vl[vl_cnt++] = i;

	if (!vl_cnt)
		return -EBUSY;

	udev->qos.vl_num = vl_cnt;

	ubase_dbg(udev, "ctrlq query vl_bitmap = %lx.\n", vl_bitmap);

	return 0;
}

static int ubase_ctrlq_query_sl(struct ubase_dev *udev)
{
	struct ubase_ctrlq_query_sl_resp resp = {0};
	struct ubase_ctrlq_query_sl_req req = {0};
	u8 i, unic_sl_cnt = 0, udma_sl_cnt = 0;
	struct ubase_ctrlq_msg msg = {0};
	unsigned long unic_sl_bitmap;
	unsigned long udma_sl_bitmap;
	int ret;

	msg.service_ver = UBASE_CTRLQ_SER_VER_01;
	msg.service_type = UBASE_CTRLQ_SER_TYPE_QOS;
	msg.opcode = UBASE_CTRLQ_OPC_QUERY_SL;
	msg.need_resp = 1;
	msg.is_resp = 0;
	msg.in_size = sizeof(req);
	msg.in = &req;
	msg.out_size = sizeof(resp);
	msg.out = &resp;

	ret = __ubase_ctrlq_send(udev, &msg, NULL);
	if (ret) {
		ubase_err(udev,
			  "failed to send ctrlq msg when query sl, ret = %d.\n", ret);
		return ret;
	}

	unic_sl_bitmap = le16_to_cpu(resp.unic_sl_bitmap);
	udma_sl_bitmap = le16_to_cpu(resp.udma_sl_bitmap);

	for (i = 0; i < UBASE_MAX_SL_NUM; i++) {
		if (test_bit(i, &unic_sl_bitmap))
			udev->qos.nic_sl[unic_sl_cnt++] = i;
		if (test_bit(i, &udma_sl_bitmap))
			udev->qos.sl[udma_sl_cnt++] = i;
	}

	if (!unic_sl_cnt) {
		ubase_err(udev, "nic doesn't have any sl.\n");
		return -EIO;
	}

	if (ubase_dev_udma_supported(udev) && !udma_sl_cnt) {
		ubase_err(udev, "udma doesn't have any sl.\n");
		return -EIO;
	}

	udev->qos.nic_sl_num = unic_sl_cnt;
	udev->qos.sl_num = udma_sl_cnt;

	ubase_dbg(udev, "ctrlq query unic_sl_bitmap = 0x%lx, udma_sl_bitmap = 0x%lx.\n",
		  unic_sl_bitmap, udma_sl_bitmap);

	return 0;
}

int ubase_qos_init(struct ubase_dev *udev)
{
	int ret = 0;

	if (ubase_dev_urma_supported(udev))
		ret = ubase_ctrlq_query_sl(udev);
	else if (ubase_dev_cdma_supported(udev))
		ret = ubase_ctrlq_query_vl(udev);

	if (ret)
		return ret;

	if (ubase_dev_pmu_supported(udev))
		return 0;

	return ubase_parse_sl_vl(udev);
}

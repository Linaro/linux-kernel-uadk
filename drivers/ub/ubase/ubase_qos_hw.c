// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved.
 *
 */

#include <ub/ubase/ubase_comm_qos.h>

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

static inline unsigned long ubase_convert_sl_vl_bitmap(struct ubase_dev *udev,
						       unsigned long sl_bitmap)
{
	unsigned long vl_bitmap = 0;
	u8 i;

	for (i = 0; i < UBASE_MAX_SL_NUM; i++) {
		if (!test_bit(i, &sl_bitmap))
			continue;

		vl_bitmap |= 1 << udev->qos.ue_sl_vl[i];
	}

	return vl_bitmap;
}

static void ubase_get_vl_sche_info(struct ubase_dev *udev,
				   struct ubase_sl_priqos *sl_priqos,
				   unsigned long *vl_bitmap,
				   u8 *vl_bw, u8 *vl_tsa)
{
	unsigned long sl_bitmap = sl_priqos->sl_bitmap;
	u8 i;

	*vl_bitmap = ubase_convert_sl_vl_bitmap(udev, sl_bitmap);
	for (i = 0; i < UBASE_MAX_SL_NUM; i++) {
		if (!test_bit(i, &sl_bitmap))
			continue;

		vl_bw[udev->qos.ue_sl_vl[i]] = sl_priqos->weight[i];
		vl_tsa[udev->qos.ue_sl_vl[i]] = sl_priqos->sch_mode[i];
	}
}

static void ubase_prase_tm_vl_sch_resp(struct ubase_dev *udev,
				       struct ubase_sl_priqos *sl_priqos,
				       struct ubase_cfg_tm_vl_sch_cmd *resp)
{
	unsigned long tsa_bitmap = le16_to_cpu(resp->vl_tsa);
	unsigned long sl_bitmap = sl_priqos->sl_bitmap;
	u8 i;

	for (i = 0; i < UBASE_MAX_SL_NUM; i++) {
		if (!test_bit(i, &sl_bitmap))
			continue;

		sl_priqos->weight[i] = resp->vl_bw[udev->qos.ue_sl_vl[i]];
		sl_priqos->sch_mode[i] = test_bit(udev->qos.ue_sl_vl[i],
						  &tsa_bitmap) ?
					 UBASE_SL_DWRR : UBASE_SL_SP;
	}
}

static void ubase_prase_ets_vl_sch_resp(struct ubase_dev *udev,
					struct ubase_sl_priqos *sl_priqos,
					struct ubase_cfg_ets_vl_sch_cmd *resp)
{
	unsigned long sl_bitmap = sl_priqos->sl_bitmap;
	u8 i;

	for (i = 0; i < UBASE_MAX_SL_NUM; i++) {
		if (!test_bit(i, &sl_bitmap))
			continue;

		sl_priqos->weight[i] = resp->vl_bw[udev->qos.ue_sl_vl[i]];
		sl_priqos->sch_mode[i] = sl_priqos->weight[i] ?
					 UBASE_SL_DWRR : UBASE_SL_SP;
	}
}

static int ubase_check_sp_sch_param(struct ubase_dev *udev, u8 vl_bw,
				    u32 *bw_sum, u8 vl_idx, bool is_ets)
{
	if (is_ets) {
		if (vl_bw) {
			ubase_err(udev,
				  "vl(%u) vl_bw must be 0 in ets sp mode.\n",
				  vl_idx);
			return -EINVAL;
		}

		return 0;
	}

	if (!vl_bw) {
		ubase_err(udev, "vl(%u) vl_bw cannot be 0 in tm sp mode.\n",
			  vl_idx);
		return -EINVAL;
	}

	*bw_sum += vl_bw;

	return 0;
}

static int ubase_check_dwrr_sch_param(struct ubase_dev *udev, u8 vl_bw,
				      u32 *bw_sum, u8 vl_idx)
{
	if (!vl_bw) {
		ubase_err(udev, "vl(%u) bw cannot be 0 in dwrr mode.\n",
			  vl_idx);
		return -EINVAL;
	}

	*bw_sum += vl_bw;

	return 0;
}

static int __ubase_check_qos_sch_param(struct ubase_dev *udev,
				       unsigned long vl_bitmap,
				       u8 *vl_bw, u8 *vl_tsa, bool is_ets)
{
#define UBASE_BW_PERCENT	100

	u32 bw_sum = 0;
	int ret;
	u8 i;

	for (i = 0; i < UBASE_MAX_VL_NUM; i++) {
		if (!test_bit(i, &vl_bitmap))
			continue;

		switch (vl_tsa[i]) {
		case IEEE_8021QAZ_TSA_STRICT:
			ret = ubase_check_sp_sch_param(udev, vl_bw[i], &bw_sum,
						       i, is_ets);
			if (ret)
				return ret;
			break;
		case IEEE_8021QAZ_TSA_ETS:
			ret = ubase_check_dwrr_sch_param(udev, vl_bw[i],
							 &bw_sum, i);
			if (ret)
				return ret;
			break;
		default:
			ubase_err(udev, "not support tc%u tsa model: %u\n",
				  i, vl_tsa[i]);
			return -EINVAL;
		}
	}

	if (bw_sum && bw_sum != UBASE_BW_PERCENT) {
		ubase_err(udev,
			  "the vl_bw sum does not add up to 100 in %s mode.\n",
			  is_ets ? "ets dwrr" : "tm sp/dwrr");
		return -EINVAL;
	}

	return 0;
}

static int __ubase_config_tm_vl_sch(struct ubase_dev *udev, u16 vl_bitmap,
				    u8 *vl_bw, u8 *vl_tsa)
{
	struct ubase_cfg_tm_vl_sch_cmd req = {0};
	struct ubase_cmd_buf in;
	u16 tsa_bitmap = 0;
	int ret;
	u8 i;

	/* the configuration takes effect for all entities. */
	req.bus_ue_id = cpu_to_le16(USHRT_MAX);
	req.vl_bitmap = cpu_to_le16(vl_bitmap);

	for (i = 0; i < UBASE_MAX_VL_NUM; i++)
		tsa_bitmap |= vl_tsa[i] ? 1 << i : 0;

	req.vl_tsa = cpu_to_le16(tsa_bitmap);
	memcpy(req.vl_bw, vl_bw, UBASE_MAX_VL_NUM);

	ubase_fill_inout_buf(&in, UBASE_OPC_TA_VL_SCH_CONFIG, false,
			     sizeof(req), &req);

	ret = __ubase_cmd_send_in(udev, &in);
	if (ret)
		ubase_err(udev, "failed to config tm vl sch, ret = %d", ret);

	return ret;
}

static int __ubase_config_ets_vl_sch(struct ubase_dev *udev, u16 vl_bitmap,
				     u8 *vl_bw, u32 port_bitmap)
{
	struct ubase_cfg_ets_vl_sch_cmd req = {0};
	struct ubase_cmd_buf in;
	int ret;

	req.port_bitmap = cpu_to_le32(port_bitmap);
	req.vl_bitmap = cpu_to_le16(vl_bitmap);
	memcpy(req.vl_bw, vl_bw, UBASE_MAX_VL_NUM);

	ubase_fill_inout_buf(&in, UBASE_OPC_CFG_ETS_TC_INFO, false, sizeof(req),
			     &req);

	ret = __ubase_cmd_send_in(udev, &in);
	if (ret)
		ubase_err(udev, "failed to cfg ets vl sch, ret = %d.", ret);

	return ret;
}

static int ubase_set_tm_priqos(struct ubase_dev *udev,
			       struct ubase_sl_priqos *sl_priqos)
{
	u8 vl_tsa[UBASE_MAX_VL_NUM] = {0};
	u8 vl_bw[UBASE_MAX_VL_NUM] = {0};
	unsigned long vl_bitmap = 0;
	int ret;

	ubase_get_vl_sche_info(udev, sl_priqos, &vl_bitmap, vl_bw, vl_tsa);

	ret = __ubase_check_qos_sch_param(udev, vl_bitmap, vl_bw, vl_tsa, false);
	if (ret)
		return ret;

	return __ubase_config_tm_vl_sch(udev, vl_bitmap, vl_bw, vl_tsa);
}

static int ubase_get_tm_priqos(struct ubase_dev *udev,
			       struct ubase_sl_priqos *sl_priqos)
{
	struct ubase_cfg_tm_vl_sch_cmd resp = {0};
	struct ubase_cfg_tm_vl_sch_cmd req = {0};
	struct ubase_cmd_buf in, out;
	u16 vl_bitmap;
	int ret;

	vl_bitmap = ubase_convert_sl_vl_bitmap(udev, sl_priqos->sl_bitmap);
	req.vl_bitmap = cpu_to_le16(vl_bitmap);

	ubase_fill_inout_buf(&in, UBASE_OPC_TA_VL_SCH_CONFIG, true,
			     sizeof(req), &req);
	ubase_fill_inout_buf(&out, UBASE_OPC_TA_VL_SCH_CONFIG, false,
			     sizeof(resp), &resp);

	ret = __ubase_cmd_send_inout(udev, &in, &out);
	if (ret) {
		ubase_err(udev,
			  "failed to get tm vl sch mode and weight, ret = %d.\n",
			  ret);
		return ret;
	}

	ubase_prase_tm_vl_sch_resp(udev, sl_priqos, &resp);

	return 0;
}

static int ubase_set_ets_priqos(struct ubase_dev *udev,
				struct ubase_sl_priqos *sl_priqos)
{
	u8 vl_tsa[UBASE_MAX_VL_NUM] = {0};
	u8 vl_bw[UBASE_MAX_VL_NUM] = {0};
	unsigned long vl_bitmap = 0;
	int ret;

	ubase_get_vl_sche_info(udev, sl_priqos, &vl_bitmap, vl_bw, vl_tsa);

	ret = __ubase_check_qos_sch_param(udev, vl_bitmap, vl_bw, vl_tsa, true);
	if (ret)
		return ret;

	return __ubase_config_ets_vl_sch(udev, vl_bitmap, vl_bw,
					 sl_priqos->port_bitmap);
}

int ubase_query_ets_tc(struct ubase_dev *udev, u32 port_bitmap,
		       u16 vl_bitmap, struct ubase_cfg_ets_vl_sch_cmd *resp)
{
	struct ubase_cfg_ets_vl_sch_cmd req = {0};
	struct ubase_cmd_buf in, out;
	int ret;

	req.port_bitmap = cpu_to_le32(port_bitmap);
	req.vl_bitmap = cpu_to_le16(vl_bitmap);

	ubase_fill_inout_buf(&in, UBASE_OPC_CFG_ETS_TC_INFO, true,
			     sizeof(req), &req);
	ubase_fill_inout_buf(&out, UBASE_OPC_CFG_ETS_TC_INFO, false,
			     sizeof(*resp), resp);

	ret = __ubase_cmd_send_inout(udev, &in, &out);
	if (ret)
		ubase_err(udev,
			  "failed to query ets tc info, ret = %d.\n", ret);

	return ret;
}

static int ubase_get_ets_priqos(struct ubase_dev *udev,
				struct ubase_sl_priqos *sl_priqos)
{
	struct ubase_cfg_ets_vl_sch_cmd resp = {0};
	u32 port_bitmap;
	u16 vl_bitmap;
	int ret;

	vl_bitmap = ubase_convert_sl_vl_bitmap(udev, sl_priqos->sl_bitmap);
	port_bitmap = sl_priqos->port_bitmap;

	ret = ubase_query_ets_tc(udev, port_bitmap, vl_bitmap, &resp);
	if (ret)
		return ret;

	ubase_prase_ets_vl_sch_resp(udev, sl_priqos, &resp);

	return 0;
}

int ubase_query_ets_tcg(struct ubase_dev *udev,
			struct ubase_query_ets_tcg_cmd *resp)
{
	struct ubase_query_ets_tcg_cmd req = {0};
	struct ubase_cmd_buf in, out;
	int ret;

	ubase_fill_inout_buf(&in, UBASE_OPC_QUERY_ETS_TCG_INFO, true,
			     sizeof(req), &req);
	ubase_fill_inout_buf(&out, UBASE_OPC_QUERY_ETS_TCG_INFO, false,
			     sizeof(*resp), resp);

	ret = __ubase_cmd_send_inout(udev, &in, &out);
	if (ret)
		ubase_err(udev,
			  "failed to query ets tcg info, ret = %d.\n", ret);

	return ret;
}

int ubase_query_ets_port(struct ubase_dev *udev,
			 struct ubase_query_ets_port_cmd *resp)
{
	struct ubase_query_ets_port_cmd req = {0};
	struct ubase_cmd_buf in, out;
	int ret;

	ubase_fill_inout_buf(&in, UBASE_OPC_QUERY_ETS_PORT_INFO, true,
			     sizeof(req), &req);
	ubase_fill_inout_buf(&out, UBASE_OPC_QUERY_ETS_PORT_INFO, false,
			     sizeof(*resp), resp);

	ret = __ubase_cmd_send_inout(udev, &in, &out);
	if (ret)
		ubase_err(udev,
			  "failed to query ets port info, ret = %d.\n", ret);

	return ret;
}

int ubase_query_fst_fvt_rqmt(struct ubase_dev *udev,
			     struct ubase_query_fst_fvt_rqmt_cmd *resp,
			     u16 bus_ue_id)
{
	struct ubase_query_fst_fvt_rqmt_cmd req = {0};
	struct ubase_cmd_buf in, out;
	int ret;

	req.bus_ue_id = cpu_to_le16(bus_ue_id);

	ubase_fill_inout_buf(&in, UBASE_OPC_QUERY_FST_FVT_RQMT, true,
			     sizeof(req), &req);
	ubase_fill_inout_buf(&out, UBASE_OPC_QUERY_FST_FVT_RQMT, false,
			     sizeof(*resp), resp);

	ret = __ubase_cmd_send_inout(udev, &in, &out);
	if (ret == -EPERM)
		return -EOPNOTSUPP;
	if (ret)
		ubase_err(udev,
			  "failed to query fst fvt rqmt info, ret=%d.\n", ret);

	return ret;
}

int ubase_check_qos_sch_param(struct auxiliary_device *adev, u16 vl_bitmap,
			      u8 *vl_bw, u8 *vl_tsa, bool is_ets)
{
	struct ubase_dev *udev;

	if (!adev || !vl_tsa || !vl_bw)
		return -EINVAL;

	udev = __ubase_get_udev_by_adev(adev);

	return __ubase_check_qos_sch_param(udev, vl_bitmap, vl_bw, vl_tsa,
					   is_ets);
}
EXPORT_SYMBOL(ubase_check_qos_sch_param);

int ubase_config_tm_vl_sch(struct auxiliary_device *adev, u16 vl_bitmap,
			   u8 *vl_bw, u8 *vl_tsa)
{
	struct ubase_dev *udev;

	if (!adev || !vl_bw  || !vl_tsa)
		return -EINVAL;

	udev = __ubase_get_udev_by_adev(adev);

	return __ubase_config_tm_vl_sch(udev, vl_bitmap, vl_bw, vl_tsa);
}
EXPORT_SYMBOL(ubase_config_tm_vl_sch);

int ubase_set_priqos_info(struct device *dev, struct ubase_sl_priqos *sl_priqos)
{
	struct ubase_dev *udev;

	if (!dev || !sl_priqos || !sl_priqos->sl_bitmap)
		return -EINVAL;

	udev = dev_get_drvdata(dev);

	if (sl_priqos->port_bitmap)
		return ubase_set_ets_priqos(udev, sl_priqos);

	return ubase_set_tm_priqos(udev, sl_priqos);
}
EXPORT_SYMBOL(ubase_set_priqos_info);

int ubase_get_priqos_info(struct device *dev, struct ubase_sl_priqos *sl_priqos)
{
	struct ubase_dev *udev;

	if (!dev || !sl_priqos || !sl_priqos->sl_bitmap)
		return -EINVAL;

	udev = dev_get_drvdata(dev);

	if (sl_priqos->port_bitmap)
		return ubase_get_ets_priqos(udev, sl_priqos);

	return ubase_get_tm_priqos(udev, sl_priqos);
}
EXPORT_SYMBOL(ubase_get_priqos_info);

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

static bool ubase_is_udma_tp_vl(struct ubase_adev_qos *qos, u8 vl)
{
	u8 i;

	for (i = 0; i < qos->tp_vl_num; i++) {
		if (qos->tp_req_vl[i] == vl)
			return true;
	}

	return false;
}

void ubase_update_udma_dscp_vl(struct auxiliary_device *adev, u8 *dscp_vl,
			       u8 dscp_num)
{
	struct ubase_adev_qos *qos;
	u8 i, arr_len;

	if (!adev || !dscp_vl)
		return;

	qos = ubase_get_adev_qos(adev);
	arr_len = min(UBASE_MAX_DSCP, dscp_num);

	for (i = 0; i < arr_len; i++)
		qos->dscp_vl[i] = ubase_is_udma_tp_vl(qos, dscp_vl[i]) ?
				       dscp_vl[i] : qos->tp_req_vl[0];
}
EXPORT_SYMBOL(ubase_update_udma_dscp_vl);

int ubase_query_tm_queue(struct ubase_dev *udev, u16 bus_ue_id,
			 struct ubase_query_tm_queue_cmd *resp)
{
	struct ubase_query_tm_queue_cmd req = {0};
	struct ubase_cmd_buf in, out;
	int ret;

	req.bus_ue_id = cpu_to_le16(bus_ue_id);

	ubase_fill_inout_buf(&in, UBASE_OPC_QUERY_TM_Q_INFO, true,
			     sizeof(req), &req);
	ubase_fill_inout_buf(&out, UBASE_OPC_QUERY_TM_Q_INFO, false,
			     sizeof(*resp), resp);

	ret = __ubase_cmd_send_inout(udev, &in, &out);
	if (ret == -EPERM)
		return -EOPNOTSUPP;
	if (ret)
		ubase_err(udev,
			  "failed to query tm queue info, bus_ue_id=%u, ret=%d.\n",
			  bus_ue_id, ret);
	return ret;
}

int ubase_query_tm_qset(struct ubase_dev *udev, u16 bus_ue_id,
			struct ubase_query_tm_qset_cmd *resp)
{
	struct ubase_query_tm_qset_cmd req = {0};
	struct ubase_cmd_buf in, out;
	int ret;

	req.bus_ue_id = cpu_to_le16(bus_ue_id);

	ubase_fill_inout_buf(&in, UBASE_OPC_QUERY_TM_QS_INFO, true,
			     sizeof(req), &req);
	ubase_fill_inout_buf(&out, UBASE_OPC_QUERY_TM_QS_INFO, false,
			     sizeof(*resp), resp);

	ret = __ubase_cmd_send_inout(udev, &in, &out);
	if (ret == -EPERM)
		return -EOPNOTSUPP;
	if (ret)
		ubase_err(udev,
			  "failed to query tm qset info, bus_ue_id = %u, ret = %d.\n",
			  bus_ue_id, ret);
	return ret;
}

int ubase_query_tm_pri(struct ubase_dev *udev, u16 bus_ue_id,
		       struct ubase_query_tm_pri_cmd *resp)
{
	struct ubase_query_tm_pri_cmd req = {0};
	struct ubase_cmd_buf in, out;
	int ret;

	req.bus_ue_id = cpu_to_le16(bus_ue_id);

	ubase_fill_inout_buf(&in, UBASE_OPC_QUERY_TM_PRI_INFO, true,
			     sizeof(req), &req);
	ubase_fill_inout_buf(&out, UBASE_OPC_QUERY_TM_PRI_INFO, false,
			     sizeof(*resp), resp);

	ret = __ubase_cmd_send_inout(udev, &in, &out);
	if (ret == -EPERM)
		return -EOPNOTSUPP;
	if (ret)
		ubase_err(udev,
			  "failed to query tm pri info, bus_ue_id = %u, ret = %d.\n",
			  bus_ue_id, ret);
	return ret;
}

int ubase_query_tm_pg(struct ubase_dev *udev, u16 bus_ue_id,
		      struct ubase_query_tm_pg_cmd *resp)
{
	struct ubase_query_tm_pg_cmd req = {0};
	struct ubase_cmd_buf in, out;
	int ret;

	req.bus_ue_id = cpu_to_le16(bus_ue_id);

	ubase_fill_inout_buf(&in, UBASE_OPC_QUERY_TM_PG_INFO, true,
			     sizeof(req), &req);
	ubase_fill_inout_buf(&out, UBASE_OPC_QUERY_TM_PG_INFO, false,
			     sizeof(*resp), resp);

	ret = __ubase_cmd_send_inout(udev, &in, &out);
	if (ret == -EPERM)
		return -EOPNOTSUPP;
	if (ret)
		ubase_err(udev,
			  "failed to query tm pg info, bus_ue_id = %u, ret = %d.\n",
			  bus_ue_id, ret);
	return ret;
}

int ubase_query_tm_port(struct ubase_dev *udev,
			struct ubase_query_tm_port_cmd *resp)
{
	struct ubase_query_tm_port_cmd req = {0};
	struct ubase_cmd_buf in, out;
	int ret;

	ubase_fill_inout_buf(&in, UBASE_OPC_QUERY_TM_PORT_INFO, true,
			     sizeof(req), &req);
	ubase_fill_inout_buf(&out, UBASE_OPC_QUERY_TM_PORT_INFO, false,
			     sizeof(*resp), resp);

	ret = __ubase_cmd_send_inout(udev, &in, &out);
	if (ret == -EPERM)
		return -EOPNOTSUPP;
	if (ret)
		ubase_err(udev, "failed to query tm port info, ret = %d.\n", ret);
	return ret;
}

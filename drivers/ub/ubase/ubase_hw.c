// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved.
 *
 */

#include "ubase_cmd.h"
#include "ubase_hw.h"

struct ubase_dma_buf_desc {
	struct ubase_dma_buf	*buf;
	u16			opc;
	bool (*is_supported)(struct ubase_dev *dev);
};

#define UBASE_DEFINE_DMA_BUFS(udev) \
	struct ubase_dma_buf_desc bufs[] = { \
	}

static void ubase_assign_addr_val(void *addr, u32 val, u8 size)
{
#define VAL_SIZE_1	1
#define VAL_SIZE_2	2
#define VAL_SIZE_4	4

	switch (size) {
	case VAL_SIZE_1:
		*(u8 *)addr = (u8)val;
		break;
	case VAL_SIZE_2:
		*(u16 *)addr = (u16)val;
		break;
	case VAL_SIZE_4:
		*(u32 *)addr = (u32)val;
		break;
	default:
		break;
	}
}

static void ubase_check_dev_caps_comm(struct ubase_dev *udev)
{
	struct ubase_caps *caps = &udev->caps.dev_caps;
	struct ubase_caps_item items[] = {
		{
			&caps->num_ceq_vectors, UBASE_DEF_CEQ_VECTOR_NUM,
			sizeof(caps->num_ceq_vectors), "ceq vector num"
		},
		{
			&caps->num_aeq_vectors, UBASE_DEF_AEQ_VECTOR_NUM,
			sizeof(caps->num_aeq_vectors), "aeq vector num"
		},
		{
			&caps->num_misc_vectors, UBASE_DEF_MISC_VERCTOR_NUM,
			sizeof(caps->num_misc_vectors), "misc vector num"
		},
		{
			&caps->public_jetty_cnt, UBASE_DEF_PUBLIC_JETTY_CNT,
			sizeof(caps->public_jetty_cnt), "public jetty count"
		},
		{
			&caps->aeqe_size, UBASE_DEF_EQE_SIZE,
			sizeof(caps->aeqe_size), "aeqe size"
		},
		{
			&caps->ceqe_size, UBASE_DEF_EQE_SIZE,
			sizeof(caps->ceqe_size), "ceqe size"
		},
		{
			&caps->aeqe_depth, UBASE_DEF_AEQ_DEPTH,
			sizeof(caps->aeqe_depth), "aeqe depth"
		},
		{
			&caps->ceqe_depth, UBASE_DEF_CEQ_DEPTH,
			sizeof(caps->ceqe_depth), "ceqe depth"
		},
	};
	u32 i, items_size = ARRAY_SIZE(items);
	u32 val;

	for (i = 0; i < items_size; i++) {
		val = 0;
		memcpy(&val, items[i].p, items[i].size);
		if (!val) {
			ubase_assign_addr_val(items[i].p, items[i].default_val,
					      items[i].size);
			ubase_warn(udev, "using default %s(%u).\n",
				   items[i].name, items[i].default_val);
		}
	}
}

static int ubase_check_dev_caps_extdb(struct ubase_dev *udev)
{
	UBASE_DEFINE_DMA_BUFS(udev);
	int i;

	for (i = 0; i < ARRAY_SIZE(bufs); i++) {
		if (bufs[i].is_supported(udev) && !bufs[i].buf->size) {
			ubase_err(udev,
				  "failed to check caps: buf[%d] size=0.\n", i);
			return -EINVAL;
		}
	}

	return 0;
}

static int ubase_check_dev_caps(struct ubase_dev *udev)
{
	ubase_check_dev_caps_comm(udev);

	return ubase_check_dev_caps_extdb(udev);
}

static void ubase_parse_dev_caps_comm(struct ubase_dev *udev,
				      const struct ubase_res_cmd_resp *resp)
{
	struct ubase_caps *dev_caps = &udev->caps.dev_caps;

	dev_caps->num_ceq_vectors = le16_to_cpu(resp->ceq_vector_num);
	dev_caps->num_aeq_vectors = le16_to_cpu(resp->aeq_vector_num);
	dev_caps->num_misc_vectors = le16_to_cpu(resp->misc_vector_num);
	dev_caps->aeqe_size = le16_to_cpu(resp->aeqe_size);
	dev_caps->ceqe_size = le16_to_cpu(resp->ceqe_size);
	dev_caps->aeqe_depth = le32_to_cpu(resp->aeqe_depth);
	dev_caps->ceqe_depth = le32_to_cpu(resp->ceqe_depth);
	dev_caps->total_ue_num = le32_to_cpu(resp->total_ue_num);
	dev_caps->public_jetty_cnt = le32_to_cpu(resp->public_jetty_cnt);
	dev_caps->rsvd_jetty_cnt = le16_to_cpu(resp->rsvd_jetty_cnt);
	dev_caps->ue_num = resp->ue_num;
	dev_caps->mac_stats_num = le16_to_cpu(resp->mac_stats_num);

	udev->ta_ctx.extdb_buf.size = le32_to_cpu(resp->ta_extdb_buf_size);
	udev->ta_ctx.timer_buf.size = le32_to_cpu(resp->ta_timer_buf_size);
}

static void ubase_parse_dev_caps_unic(struct ubase_dev *udev,
				      const struct ubase_res_cmd_resp *resp)
{
	struct ubase_adev_caps *unic_caps = &udev->caps.unic_caps;

	unic_caps->jfs.max_cnt = le32_to_cpu(resp->nic_jfs_max_cnt);
	unic_caps->jfs.reserved_cnt = le32_to_cpu(resp->nic_jfs_reserved_cnt);
	unic_caps->jfs.depth = le32_to_cpu(resp->nic_jfs_depth);
	unic_caps->jfr.max_cnt = le32_to_cpu(resp->nic_jfr_max_cnt);
	unic_caps->jfr.reserved_cnt = le32_to_cpu(resp->nic_jfr_reserved_cnt);
	unic_caps->jfr.depth = le32_to_cpu(resp->nic_jfr_depth);
	unic_caps->jfc.max_cnt = le32_to_cpu(resp->nic_jfc_max_cnt);
	unic_caps->jfc.reserved_cnt = le32_to_cpu(resp->nic_jfc_reserved_cnt);
	unic_caps->jfc.depth = le32_to_cpu(resp->nic_jfc_depth);
	unic_caps->cqe_size = le16_to_cpu(resp->nic_cqe_size);
	unic_caps->utp_port_bitmap = le32_to_cpu(resp->port_bitmap);
}

static void ubase_parse_dev_caps_udma(struct ubase_dev *udev,
				      const struct ubase_res_cmd_resp *resp)
{
	struct ubase_adev_caps *udma_caps = &udev->caps.udma_caps;

	udma_caps->jfs.max_cnt = le32_to_cpu(resp->udma_jfs_max_cnt);
	udma_caps->jfs.reserved_cnt = le32_to_cpu(resp->udma_jfs_reserved_cnt);
	udma_caps->jfs.depth = le32_to_cpu(resp->udma_jfs_depth);
	udma_caps->jfr.max_cnt = le32_to_cpu(resp->udma_jfr_max_cnt);
	udma_caps->jfr.reserved_cnt = le32_to_cpu(resp->udma_jfr_reserved_cnt);
	udma_caps->jfr.depth = le32_to_cpu(resp->udma_jfr_depth);
	udma_caps->jfc.max_cnt = le32_to_cpu(resp->udma_jfc_max_cnt);
	udma_caps->jfc.reserved_cnt = le32_to_cpu(resp->udma_jfc_reserved_cnt);
	udma_caps->jfc.depth = le32_to_cpu(resp->udma_jfc_depth);
	udma_caps->cqe_size = le16_to_cpu(resp->udma_cqe_size);
	udma_caps->jtg_max_cnt = le32_to_cpu(resp->jtg_max_cnt);
	udma_caps->rc_max_cnt = le32_to_cpu(resp->rc_max_cnt_per_vl);
	udma_caps->rc_que_depth = le32_to_cpu(resp->udma_rc_depth);
}

static void ubase_parse_dev_caps(struct ubase_dev *udev,
				 const struct ubase_res_cmd_resp *resp)
{
	int i;

	for (i = 0; i < UBASE_CAP_LEN; i++)
		udev->cap_bits[i] = le32_to_cpu(resp->cap_bits[i]);

	ubase_parse_dev_caps_comm(udev, resp);
	ubase_parse_dev_caps_unic(udev, resp);
	ubase_parse_dev_caps_udma(udev, resp);
}

static int ubase_parse_dev_res(struct ubase_dev *udev,
			       struct ubase_res_cmd_resp *resp)
{
	ubase_parse_dev_caps(udev, resp);
	return ubase_check_dev_caps(udev);
}

int ubase_query_dev_res(struct ubase_dev *udev)
{
	struct ubase_res_cmd_resp resp = {0};
	struct ubase_cmd_buf out;
	struct ubase_cmd_buf in;
	int ret;

	__ubase_fill_inout_buf(&in, UBASE_OPC_QUERY_COMM_RSRC_PARAM, true, 0,
			       NULL);

	__ubase_fill_inout_buf(&out, UBASE_OPC_QUERY_COMM_RSRC_PARAM, false,
			       sizeof(resp), &resp);

	ret = __ubase_cmd_send_inout(udev, &in, &out);
	if (ret) {
		ubase_err(udev, "failed to query ubase res, ret = %d.\n", ret);
		return ret;
	}

	return ubase_parse_dev_res(udev, &resp);
}

int ubase_query_controller_info(struct ubase_dev *udev)
{
	struct ubase_caps *dev_caps = &udev->caps.dev_caps;
	struct ubase_query_controller_info_resp resp = {0};
	struct ubase_cmd_buf in, out;
	int ret;

	ubase_fill_inout_buf(&in, UBASE_OPC_QUERY_CTL_INFO, true, 0, NULL);
	ubase_fill_inout_buf(&out, UBASE_OPC_QUERY_CTL_INFO, true,
			     sizeof(resp), &resp);

	ret = __ubase_cmd_send_inout(udev, &in, &out);
	if (ret) {
		ubase_err(udev,
			  "failed to query controller info, ret = %d.\n", ret);
		return ret;
	}

	dev_caps->packet_pattern_mode = resp.packet_pattern_mode;
	dev_caps->ack_queue_num = resp.ack_queue_num;

	return 0;
}

int ubase_query_chip_info(struct ubase_dev *udev)
{
	struct ubase_caps *dev_caps = &udev->caps.dev_caps;
	struct ubase_query_chip_die_cmd resp = {0};
	struct ubase_cmd_buf in, out;
	int ret;

	__ubase_fill_inout_buf(&in, UBASE_OPC_QUERY_CHIP_INFO, true, 0, NULL);
	__ubase_fill_inout_buf(&out, UBASE_OPC_QUERY_CHIP_INFO, true,
			       sizeof(resp), &resp);
	ret = __ubase_cmd_send_inout(udev, &in, &out);
	if (ret) {
		ubase_err(udev,
			  "failed to query ub chip info, ret = %d.\n", ret);
		return ret;
	}

	dev_caps->nl_port_id = le16_to_cpu(resp.nl_port_id);
	dev_caps->chip_id = le16_to_cpu(resp.chip_id);
	dev_caps->die_id = le16_to_cpu(resp.die_id);
	dev_caps->io_port_id = le16_to_cpu(resp.io_port_id);
	dev_caps->ue_id = le16_to_cpu(resp.ue_id);
	dev_caps->ub_port_logic_id = le16_to_cpu(resp.ub_port_logic_id);
	dev_caps->io_port_logic_id = le16_to_cpu(resp.io_port_logic_id);
	dev_caps->nl_id = le16_to_cpu(resp.nl_id);

	return 0;
}

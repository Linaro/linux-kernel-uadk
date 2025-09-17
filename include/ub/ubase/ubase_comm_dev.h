/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved.
 *
 */

#ifndef _UB_UBASE_COMM_DEV_H_
#define _UB_UBASE_COMM_DEV_H_

#include <linux/auxiliary_bus.h>
#include <linux/dcbnl.h>
#include <linux/list.h>

struct iova_slot;

#define UBASE_ADEV_NAME	"ubase"

#define UBASE_IOVA_COMM_PFN_CNT 1
#define UBASE_MAX_DSCP		(64)
#define UBASE_MAX_SL_NUM	(16U)
#define UBASE_MAX_REQ_VL_NUM	(8U)
#define UBASE_MAX_VL_NUM	(16U)
#if UBASE_MAX_VL_NUM < IEEE_8021QAZ_MAX_TCS
#error "UBASE_MAX_VL_NUM can't less than IEEE_8021QAZ_MAX_TCS"
#endif

#define UBASE_SUP_UBL		BIT(0)
#define UBASE_SUP_ETH		BIT(1)
#define UBASE_SUP_UNIC		BIT(2)
#define UBASE_SUP_UDMA		BIT(3)
#define UBASE_SUP_CDMA		BIT(4)
#define UBASE_SUP_PMU		BIT(5)
#define UBASE_SUP_URMA		(UBASE_SUP_UNIC | UBASE_SUP_UDMA)
#define UBASE_SUP_UBL_ETH	(UBASE_SUP_UBL | UBASE_SUP_ETH)
#define UBASE_SUP_ALL		(UBASE_SUP_UNIC | UBASE_SUP_UDMA | \
				 UBASE_SUP_CDMA | UBASE_SUP_PMU)
#define UBASE_SUP_NO_PMU	(UBASE_SUP_ALL ^ UBASE_SUP_PMU)

enum ubase_reset_type {
	UBASE_NO_RESET,
	UBASE_ELR_RESET,
	UBASE_UE_RESET,
	UBASE_MAX_RESET,
};

enum ubase_reset_stage {
	UBASE_RESET_STAGE_NONE,
	UBASE_RESET_STAGE_DOWN,
	UBASE_RESET_STAGE_UNINIT,
	UBASE_RESET_STAGE_INIT,
	UBASE_RESET_STAGE_UP,
};

struct ubase_caps {
	u16	num_ceq_vectors;
	u16	num_aeq_vectors;
	u16	num_misc_vectors;

	u32	aeqe_depth;
	u32	ceqe_depth;
	u16	aeqe_size;
	u16	ceqe_size;

	u32	total_ue_num;
	u32	public_jetty_cnt;
	u8	vl_num;
	u16	rsvd_jetty_cnt;

	u8	req_vl[UBASE_MAX_VL_NUM];
	u8	resp_vl[UBASE_MAX_VL_NUM];

	u8	packet_pattern_mode;
	u8	ack_queue_num;
	u8	oor_en;
	u8	reorder_queue_en;
	u32	on_flight_size;
	u8	reorder_cap;
	u8	reorder_queue_shift;
	u8	at_times;
	u8	ue_num;
	u16	mac_stats_num;

	u32	logic_port_bitmap;
	u16	ub_port_logic_id;
	u16	io_port_logic_id;
	u16	io_port_id;
	u16	nl_port_id;
	u16	chip_id;
	u16	die_id;
	u16	ue_id;
	u16	nl_id;

	u32	tid;
	u32	eid;
	u16	upi;
	u32	ctl_no;

	u32	fw_version;
};

struct ubase_res_caps {
	u32	max_cnt;
	u32	start_idx;
	u32	reserved_cnt;
	u32	depth;
};

struct ubase_pmem_caps {
	u64		dma_len;
	dma_addr_t	dma_addr;
};

struct ubase_adev_caps {
	struct ubase_res_caps	jfs;
	struct ubase_res_caps	jfr;
	struct ubase_res_caps	jfc;
	struct ubase_res_caps	tp;
	struct ubase_res_caps	tpg;
	struct ubase_pmem_caps	pmem;
	u32			utp_port_bitmap;  /* utp port bitmap */
	u32			jtg_max_cnt;
	u32			rc_max_cnt;
	u32			rc_que_depth;
	u32			ccc_max_cnt;
	u32			dest_addr_max_cnt;
	u32			seid_upi_max_cnt;
	u32			tpm_max_cnt;
	u16			cqe_size;
};

struct ubase_ctx_buf_cap {
	dma_addr_t		dma_ctx_buf_ba; /* pass to hw */
	struct iova_slot	*slot;
	u32			entry_size;
	u32			entry_cnt;
	u32			cnt_per_page_shift;
	struct xarray		ctx_xa;
	struct mutex		ctx_mutex;
};

struct ubase_ctx_buf {
	struct ubase_ctx_buf_cap jfs;
	struct ubase_ctx_buf_cap jfr;
	struct ubase_ctx_buf_cap jfc;
	struct ubase_ctx_buf_cap jtg;
	struct ubase_ctx_buf_cap rc;

	struct ubase_ctx_buf_cap tp;
	struct ubase_ctx_buf_cap tpg;
};

struct net_device;
struct ubase_adev_com {
	struct auxiliary_device	*adev;
	struct net_device	*netdev;
};

struct ubase_resource_space {
	resource_size_t addr_unmapped;
	void __iomem *addr;
};

struct ubase_adev_qos {
	/* udma/cdma resource */
	u8	sl_num;
	u8	sl[UBASE_MAX_SL_NUM];
	u8	tp_sl_num;
	u8	tp_sl[UBASE_MAX_SL_NUM];
	u8	ctp_sl_num;
	u8	ctp_sl[UBASE_MAX_SL_NUM];

	u8	vl_num;
	u8	vl[UBASE_MAX_VL_NUM];
	u8	tp_vl_num;
	u8	tp_resp_vl_offset;
	u8	tp_req_vl[UBASE_MAX_VL_NUM];
	u8	ctp_vl_num;
	u8	ctp_resp_vl_offset;
	u8	ctp_req_vl[UBASE_MAX_VL_NUM];

	u8	dscp_vl[UBASE_MAX_DSCP];

	/* unic resource */
	u8	nic_sl_num;
	u8	nic_sl[UBASE_MAX_SL_NUM];

	u8	nic_vl_num;
	u8	nic_vl[UBASE_MAX_VL_NUM];

	/* common resource */
	u8	ue_max_vl_id;
	u8	ue_sl_vl[UBASE_MAX_SL_NUM];
};

struct ubase_ue_caps {
	u8	ceq_vector_num;
	u8	aeq_vector_num;
	u32	aeqe_depth;
	u32	ceqe_depth;
	u32	jfs_max_cnt;
	u32	jfs_depth;
	u32	jfr_max_cnt;
	u32	jfr_depth;
	u32	jfc_max_cnt;
	u32	jfc_depth;
	u32	rc_max_cnt;
	u32	rc_depth;
	u32	jtg_max_cnt;
};

#define UBASE_BUS_EID_LEN 4
struct ubase_bus_eid {
	u32 eid[UBASE_BUS_EID_LEN];
};

bool ubase_adev_ubl_supported(struct auxiliary_device *adev);
bool ubase_adev_eth_mac_supported(struct auxiliary_device *adev);

struct ubase_resource_space *ubase_get_io_base(struct auxiliary_device *adev);
struct ubase_resource_space *ubase_get_mem_base(struct auxiliary_device *adev);
struct ubase_caps *ubase_get_dev_caps(struct auxiliary_device *adev);
struct ubase_adev_caps *ubase_get_unic_caps(struct auxiliary_device *adev);
struct ubase_adev_caps *ubase_get_udma_caps(struct auxiliary_device *adev);
struct ubase_adev_caps *ubase_get_cdma_caps(struct auxiliary_device *adev);

void ubase_virt_register(struct auxiliary_device *adev,
			 void (*virt_handler)(struct auxiliary_device *adev,
					      u16 bus_ue_id, bool is_en));
void ubase_virt_unregister(struct auxiliary_device *adev);

int ubase_get_bus_eid(struct auxiliary_device *adev, struct ubase_bus_eid *eid);

#endif

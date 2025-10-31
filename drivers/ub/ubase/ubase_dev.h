/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved.
 *
 */

#ifndef __UBASE_DEV_H__
#define __UBASE_DEV_H__

#include <linux/auxiliary_bus.h>
#include <linux/dma-mapping.h>
#include <ub/ubase/ubase_comm_cmd.h>
#include <ub/ubase/ubase_comm_debugfs.h>
#include <ub/ubase/ubase_comm_dev.h>
#include <ub/ubase/ubase_comm_eq.h>
#include <ub/ubase/ubase_comm_hw.h>
#include <ub/ubase/ubase_comm_stats.h>

#include "ubase.h"
#include "ubase_eq.h"
#include "ubase_ubus.h"

#define UBASE_MOD_VERSION		"1.0"

#define ubase_dbg(_udev, fmt, ...) do {	                                      \
	if (ubase_dbg_default())                                              \
		dev_info(_udev->dev, "(pid %d) " fmt,                         \
			 current->pid, ##__VA_ARGS__);                        \
	} while (0)

#define ubase_err(_udev, fmt, ...)                                            \
	dev_err(_udev->dev, "(pid %d) " fmt,                                  \
		current->pid, ##__VA_ARGS__)

#define ubase_info(_udev, fmt, ...)                                           \
	dev_info(_udev->dev, "(pid %d) " fmt,                                 \
		 current->pid, ##__VA_ARGS__)

#define ubase_warn(_udev, fmt, ...)                                           \
	dev_warn(_udev->dev, "(pid %d) " fmt,                                 \
		 current->pid, ##__VA_ARGS__)

struct ubase_adev {
	struct auxiliary_device adev;
	struct ubase_dev *udev;
	struct atomic_notifier_head	comp_nh;
	struct notifier_block	comp_notifier;
	int idx;

	struct mutex	virt_lock;
	void (*virt_handler)(struct auxiliary_device *adev, u16 bus_ue_id,
			     bool is_en);
	struct mutex	port_lock;
	void (*port_handler)(struct auxiliary_device *adev, bool link_up);
	struct mutex	reset_lock;
	void (*reset_handler)(struct auxiliary_device *adev,
			      enum ubase_reset_stage stage);
	struct mutex	activate_lock;
	void (*activate_handler)(struct auxiliary_device *adev, bool activate);
};

struct ubase_priv {
	struct ubase_adev *uadev[UBASE_DRV_MAX];
	struct mutex uadev_lock; /* protect uadev[] */
};

struct ubase_dev_caps {
	struct ubase_adev_caps	udma_caps;
	struct ubase_adev_caps	unic_caps;
	struct ubase_caps	dev_caps;
	struct ubase_ue_caps	ue_caps;
};

struct ubase_mbox_cmd {
	struct dma_pool *pool;
	struct semaphore sem;
	struct ubase_mbx_event_context ctx;
};

struct ubase_dma_buf {
	void		*addr;
	dma_addr_t	dma_addr;
	size_t		size;
};

struct ubase_ta_layer_ctx {
	struct ubase_dma_buf	extdb_buf;
	struct ubase_dma_buf	timer_buf;
};

struct ubase_tp_layer_ctx {
	spinlock_t		tpg_lock;
	struct ubase_tpg	*tpg;
};

struct ubase_reset_stat {
	u32 reset_done_cnt;
	u32 hw_reset_done_cnt;
	u32 elr_reset_cnt;
	u32 reset_fail_cnt;
	u32 reset_retry_cnt;
	u32 port_reset_cnt;
	u32 himac_reset_cnt;
};

enum ubase_dev_state_bit {
	UBASE_STATE_INITED_B,
	UBASE_STATE_DISABLED_B,
	UBASE_STATE_RST_HANDLING_B,
	UBASE_STATE_IRQ_INVALID_B,
	UBASE_STATE_PORT_RESETTING_B,
	UBASE_STATE_HIMAC_RESETTING_B,
	UBASE_STATE_CTX_READY_B,
	UBASE_STATE_PREALLOC_OK_B,
};

struct ubase_crq_event_nbs {
	struct list_head list;
	struct ubase_crq_event_nb nb;
};

struct ubase_crq_table {
	struct mutex lock;
	unsigned long last_crq_scheduled;
	struct ubase_crq_event_nbs nbs;
};

#define UBASE_ACT_STAT_MAX_NUM 10U
struct ubase_activate_dev_stats {
	u64	act_cnt;
	u64	deact_cnt;
	struct {
		bool		activate;
		int		result;
		time64_t	time;
	} stats[UBASE_ACT_STAT_MAX_NUM];
	struct mutex	lock;
};

struct ubase_stats {
	struct mutex			stats_lock;
	struct ubase_eth_mac_stats	eth_stats;
	struct ubase_activate_dev_stats	activate_record;
};

struct ubase_act_info {
	u16			wait_msn;
	int			result;
	struct completion	activate_done;
};

struct ubase_act_ctx {
	u16			msn;
	struct ubase_act_info	self;
	struct ubase_act_info	other;
	struct mutex		lock;
};

struct ubase_arq_msg {
	u16 opcode;
	void *data;
	u32 data_len;
};

#define MAX_ARQ_MSG_NUM 128
struct ubase_arq_msg_ring {
	u8 pi;
	u8 ci;
	atomic_t count;
	struct ubase_arq_msg msg[MAX_ARQ_MSG_NUM];
};

struct ubase_pmem_ctx {
	u16			page_cnt;
	struct page		**pgs;
	struct scatterlist	*sg;
	dma_addr_t		dma_addr;
};

struct ubase_prealloc_mem_info {
	struct ubase_pmem_ctx	comm;
	struct ubase_pmem_ctx	udma;
};

struct ubase_dev {
	struct device		*dev;
	int			dev_id;
	struct ubase_priv	priv;
	struct ubase_hw		hw;

	struct ubase_dev_caps	caps;
	struct ubase_adev_qos	qos;
	struct ubase_dbgfs	dbgfs;
	struct ubase_ctx_buf	ctx_buf;
	struct ubase_ta_layer_ctx	ta_ctx;
	struct ubase_tp_layer_ctx	tp_ctx;
	u32			cap_bits[UBASE_CAP_LEN];
	struct ubase_irq_table	irq_table;
	struct ubase_mbox_cmd	mb_cmd;
	struct workqueue_struct	*ubase_wq;
	struct workqueue_struct	*ubase_async_wq;
	struct workqueue_struct	*ubase_reset_wq;
	struct workqueue_struct	*ubase_period_wq;
	struct workqueue_struct	*ubase_arq_wq;
	unsigned long		serv_proc_cnt;
	struct ubase_delay_work	service_task;
	struct ubase_delay_work	reset_service_task;
	struct ubase_delay_work	period_service_task;
	struct ubase_delay_work	arq_service_task;
	struct ubase_crq_table	crq_table;
	unsigned long		state_bits;
	struct list_head	ue_list;
	struct mutex		ue_list_lock;

	struct ubase_reset_stat	reset_stat;
	enum ubase_reset_type	reset_type;
	unsigned long		last_reset_scheduled;
	enum ubase_reset_stage	reset_stage;
	struct ubase_stats	stats;
	struct ubase_act_ctx	act_ctx;
	struct ubase_arq_msg_ring	arq;
	struct ubase_prealloc_mem_info	pmem_info;
};

#define UBASE_ERR_MSG_LEN	128
struct ubase_init_function {
	char err_msg[UBASE_ERR_MSG_LEN];
	u32 support_devs;
	u8 need_reset;
	int (*init_func)(struct ubase_dev *udev);
	void (*uninit_func)(struct ubase_dev *udev);
};

bool ubase_dbg_default(void);
bool ubase_dev_urma_supported(struct ubase_dev *udev);
bool ubase_dev_unic_supported(struct ubase_dev *udev);
bool ubase_dev_cdma_supported(struct ubase_dev *udev);
bool ubase_dev_pmu_supported(struct ubase_dev *udev);
bool ubase_dev_fwctl_supported(struct ubase_dev *udev);

static inline
struct ubase_dev *__ubase_get_udev_by_adev(struct auxiliary_device *adev)
{
	struct ubase_adev *uadev = container_of(adev, struct ubase_adev, adev);

	return uadev->udev;
}

static inline bool ubase_dev_uvb_supported(struct ubase_dev *udev)
{
	return ubase_get_cap_bit(udev, UBASE_SUPPORT_UVB_B);
}

static inline
struct ubase_dev *ubase_get_udev_by_adev(struct auxiliary_device *adev)
{
	if (!adev)
		return NULL;

	return __ubase_get_udev_by_adev(adev);
}

static inline bool ubase_dev_udma_supported(struct ubase_dev *udev)
{
	return ubase_dev_urma_supported(udev) &&
	       !ubase_get_cap_bit(udev, UBASE_SUPPORT_UDMA_DISABLE_B);
}

static inline bool ubase_dev_ubl_supported(struct ubase_dev *udev)
{
	return ubase_get_cap_bit(udev, UBASE_SUPPORT_UBL_B);
}

static inline bool ubase_dev_eth_mac_supported(struct ubase_dev *udev)
{
	return ubase_get_cap_bit(udev, UBASE_SUPPORT_ETH_MAC_B);
}

int ubase_adev_idx_alloc(void);
void ubase_adev_idx_free(int id);

void ubase_port_down(struct ubase_dev *udev);
void ubase_port_up(struct ubase_dev *udev);

int ubase_dev_init(struct ubase_dev *udev);
void ubase_dev_uninit(struct ubase_dev *udev);

#endif

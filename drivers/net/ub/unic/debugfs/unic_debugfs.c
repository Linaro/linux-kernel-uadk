// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved.
 *
 */

#include <net/page_pool/types.h>
#include <linux/debugfs.h>
#include <linux/time.h>
#include <ub/ubase/ubase_comm_cmd.h>
#include <ub/ubase/ubase_comm_mbx.h>

#include "unic_dev.h"
#include "unic_hw.h"
#include "unic_debugfs.h"

static int unic_dbg_dump_dev_info(struct seq_file *s, void *data)
{
	struct unic_dev *unic_dev = dev_get_drvdata(s->private);
	struct net_device *netdev = unic_dev->comdev.netdev;
	u16 max_frame_size;
	int ret;

	seq_printf(s, "%-25s", "DEV_NAME:");
	seq_printf(s, "%-12s\n", netdev->name);

	if (!unic_dev_ubl_supported(unic_dev)) {
		ret = unic_check_validate_dump_mtu(unic_dev, netdev->mtu,
						   &max_frame_size);
		if (!ret) {
			seq_printf(s, "%-25s", "MAX_FRAME_SIZE:");
			seq_printf(s, "%-12u\n", max_frame_size);
		}
	}

	return 0;
}

static const struct unic_dbg_cap_bit_info {
	const char *format;
	bool (*get_bit)(struct unic_dev *dev);
} unic_cap_bits[] = {
	{"\tsupport_ubl: %u\n", &unic_dev_ubl_supported},
	{"\tsupport_ets: %u\n", &unic_dev_ets_supported},
	{"\tsupport_fec: %u\n", &unic_dev_fec_supported},
	{"\tsupport_tc_speed_limit: %u\n", &unic_dev_tc_speed_limit_supported},
	{"\tsupport_tx_csum_offload: %u\n", &unic_dev_tx_csum_offload_supported},
	{"\tsupport_rx_csum_offload: %u\n", &unic_dev_rx_csum_offload_supported},
	{"\tsupport_fec_stats: %u\n", &unic_dev_fec_stats_supported},
};

static void unic_dbg_dump_caps_bits(struct unic_dev *unic_dev,
				    struct seq_file *s)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(unic_cap_bits); i++)
		seq_printf(s, unic_cap_bits[i].format,
			   unic_cap_bits[i].get_bit(unic_dev));
}

static void unic_dbg_dump_caps(struct unic_dev *unic_dev, struct seq_file *s)
{
	struct unic_caps *unic_caps = &unic_dev->caps;
	struct unic_dbg_caps_info {
		const char *format;
		u32 caps_info;
	} unic_caps_info[] = {
		{"\ttotal_ip_tbl_size: %hu\n", unic_caps->total_ip_tbl_size},
		{"\tmax_trans_unit: %hu\n", unic_caps->max_trans_unit},
		{"\tmin_trans_unit: %hu\n", unic_caps->min_trans_unit},
		{"\tvport_buf_size: %u\n", unic_caps->vport_buf_size},
		{"\tvport_buf_num: %hhu\n", unic_caps->vport_buf_num},
		{"\tmax_int_ql: %hu\n", unic_caps->max_int_ql},
		{"\tmax_int_gl: %hu\n", unic_caps->max_int_gl},
	};
	int i;

	for (i = 0; i < ARRAY_SIZE(unic_caps_info); i++)
		seq_printf(s, unic_caps_info[i].format,
			   unic_caps_info[i].caps_info);
}

static int unic_dbg_dump_caps_info(struct seq_file *s, void *data)
{
	struct unic_dev *unic_dev = dev_get_drvdata(s->private);

	seq_puts(s, "CAP_BITS:\n");
	unic_dbg_dump_caps_bits(unic_dev, s);
	seq_puts(s, "\nCAPS:\n");
	unic_dbg_dump_caps(unic_dev, s);

	return 0;
}

static void unic_dump_page_pool_info(struct page_pool *page_pool,
				     struct seq_file *s, u32 index)
{
#define UNIC_SIZE_1K	1024

	seq_printf(s, "%-10u", index);
	seq_printf(s, "%-14u", READ_ONCE(page_pool->pages_state_hold_cnt));
	seq_printf(s, "%-14d", atomic_read(&page_pool->pages_state_release_cnt));
	seq_printf(s, "%-21u", page_pool->p.pool_size);
	seq_printf(s, "%-7u", page_pool->p.order);
	seq_printf(s, "%-9d", page_pool->p.nid);
	seq_printf(s, "%uK\n", page_pool->p.max_len / UNIC_SIZE_1K);
}

static int unic_dbg_dump_page_pool_info(struct seq_file *s, void *data)
{
	struct unic_dev *unic_dev = dev_get_drvdata(s->private);
	struct unic_rq *rq;
	int ret = 0;
	u32 i;

	seq_puts(s, "QUEUE_ID  ALLOCATE_CNT  FREE_CNT      POOL_SIZE(PAGE_NUM)  ");
	seq_puts(s, "ORDER  NUMA_ID  MAX_LEN\n");

	if (!mutex_trylock(&unic_dev->channels.mutex))
		return -EBUSY;

	if (__unic_resetting(unic_dev) ||
	    !unic_dev->channels.c) {
		ret = -EBUSY;
		goto out;
	}

	for (i = 0; i < unic_dev->channels.num; i++) {
		rq = unic_dev->channels.c[i].rq;
		unic_dump_page_pool_info(rq->page_pool, s, i);
	}

out:
	mutex_unlock(&unic_dev->channels.mutex);
	return ret;
}

static bool unic_dbg_dentry_support(struct device *dev, u32 property)
{
	struct unic_dev *unic_dev = dev_get_drvdata(dev);

	return ubase_dbg_dentry_support(unic_dev->comdev.adev, property);
}

static struct ubase_dbg_dentry_info unic_dbg_dentry[] = {
	/* keep unic at the bottom and add new directory above */
	{
		.name = "unic",
		.property = UBASE_SUP_UNIC | UBASE_SUP_UBL,
		.support = unic_dbg_dentry_support,
	},
};

static struct ubase_dbg_cmd_info unic_dbg_cmd[] = {
	{
		.name = "dev_info",
		.dentry_index = UNIC_DBG_DENTRY_ROOT,
		.property = UBASE_SUP_UNIC | UBASE_SUP_UBL,
		.support = unic_dbg_dentry_support,
		.init = ubase_dbg_seq_file_init,
		.read_func = unic_dbg_dump_dev_info,
	}, {
		.name = "caps_info",
		.dentry_index = UNIC_DBG_DENTRY_ROOT,
		.property = UBASE_SUP_UNIC | UBASE_SUP_UBL,
		.support = unic_dbg_dentry_support,
		.init = ubase_dbg_seq_file_init,
		.read_func = unic_dbg_dump_caps_info,
	}, {
		.name = "page_pool_info",
		.dentry_index = UNIC_DBG_DENTRY_ROOT,
		.property = UBASE_SUP_UNIC | UBASE_SUP_UBL,
		.support = unic_dbg_dentry_support,
		.init = ubase_dbg_seq_file_init,
		.read_func = unic_dbg_dump_page_pool_info,
	}
};

int unic_dbg_init(struct auxiliary_device *adev)
{
	struct dentry *ubase_root_dentry = unic_get_ubase_root_dentry(adev);
	struct ubase_dbg_dentry_info dentry[UNIC_DBG_DENTRY_ROOT + 1] = {0};
	u8 dentry_num = ARRAY_SIZE(unic_dbg_dentry);
	struct device *dev = &adev->dev;
	struct unic_dev *unic_dev;
	int ret;

	unic_dev = (struct unic_dev *)dev_get_drvdata(dev);

	if (!ubase_root_dentry) {
		unic_err(unic_dev, "dbgfs root dentry does not exist.\n");
		return -ENOENT;
	}

	unic_dev->dbgfs.dentry = debugfs_create_dir(unic_dbg_dentry[dentry_num - 1].name,
						    ubase_root_dentry);
	if (IS_ERR(unic_dev->dbgfs.dentry)) {
		unic_err(unic_dev, "failed to create unic debugfs root dir.\n");
		return PTR_ERR(unic_dev->dbgfs.dentry);
	}

	memcpy(dentry, unic_dbg_dentry, sizeof(dentry));
	dentry[UNIC_DBG_DENTRY_ROOT].dentry = unic_dev->dbgfs.dentry;
	unic_dev->dbgfs.cmd_info = unic_dbg_cmd;
	unic_dev->dbgfs.cmd_info_size = ARRAY_SIZE(unic_dbg_cmd);

	ret = ubase_dbg_create_dentry(dev, &unic_dev->dbgfs, dentry,
				      ARRAY_SIZE(dentry) - 1);
	if (ret) {
		unic_err(unic_dev,
			 "failed to create unic debugfs dentry, ret = %d.\n",
			 ret);
		goto create_dentry_err;
	}

	return 0;

create_dentry_err:
	debugfs_remove_recursive(unic_dev->dbgfs.dentry);

	return ret;
}

void unic_dbg_uninit(struct auxiliary_device *adev)
{
	struct unic_dev *unic_dev = dev_get_drvdata(&adev->dev);

	debugfs_remove_recursive(unic_dev->dbgfs.dentry);
}

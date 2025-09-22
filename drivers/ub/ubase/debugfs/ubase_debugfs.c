// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved.
 *
 */

#include <linux/debugfs.h>
#include <linux/kernel.h>
#include <ub/ubase/ubase_comm_debugfs.h>

#include "ubase_dev.h"
#include "ubase_qos_debugfs.h"
#include "ubase_debugfs.h"

static struct dentry *ubase_dbgfs_root;

static int ubase_dbg_dump_rst_info(struct seq_file *s, void *data)
{
	struct ubase_dev *udev = dev_get_drvdata(s->private);

	seq_printf(s, "ELR reset count: %u\n", udev->reset_stat.elr_reset_cnt);
	seq_printf(s, "port reset count: %u\n", udev->reset_stat.port_reset_cnt);
	seq_printf(s, "reset done count: %u\n", udev->reset_stat.reset_done_cnt);
	seq_printf(s, "HW reset done count: %u\n", udev->reset_stat.hw_reset_done_cnt);
	seq_printf(s, "reset fail count: %u\n", udev->reset_stat.reset_fail_cnt);
	seq_printf(s, "udev state: 0x%lx\n", udev->state_bits);

	return 0;
}


static bool __ubase_dbg_dentry_support(struct device *dev, u32 property)
{
	struct ubase_dev *udev = dev_get_drvdata(dev);

	if (((property & UBASE_SUP_UNIC) && ubase_dev_unic_supported(udev)) ||
	    ((property & UBASE_SUP_UDMA) && ubase_dev_udma_supported(udev)) ||
	    ((property & UBASE_SUP_CDMA) && ubase_dev_cdma_supported(udev)) ||
	    ((property & UBASE_SUP_PMU) && ubase_dev_pmu_supported(udev))) {
		if (((property & UBASE_SUP_UBL) && ubase_dev_ubl_supported(udev)) ||
		    ((property & UBASE_SUP_ETH) && ubase_dev_eth_mac_supported(udev)))
			return true;
	}

	return false;
}

bool ubase_dbg_dentry_support(struct auxiliary_device *adev, u32 property)
{
	if (!adev)
		return false;

	return __ubase_dbg_dentry_support(__ubase_get_udev_by_adev(adev)->dev,
					  property);
}
EXPORT_SYMBOL(ubase_dbg_dentry_support);

static int __ubase_dbg_seq_file_init(struct device *dev,
				     struct ubase_dbg_dentry_info *dirs,
				     struct ubase_dbgfs *dbgfs, u32 idx)
{
	struct ubase_dbg_cmd_info *cmd_info = &dbgfs->cmd_info[idx];
	struct dentry *cur_dir;

	cur_dir = dirs[cmd_info->dentry_index].dentry;
	if (!cmd_info->read_func)
		return -EFAULT;

	debugfs_create_devm_seqfile(dev, cmd_info->name, cur_dir,
				    cmd_info->read_func);

	return 0;
}

int ubase_dbg_seq_file_init(struct device *dev,
			    struct ubase_dbg_dentry_info *dirs,
			    struct ubase_dbgfs *dbgfs, u32 idx)
{
	if (!dev || !dirs || !dbgfs || !dbgfs->cmd_info)
		return -EINVAL;

	return __ubase_dbg_seq_file_init(dev, dirs, dbgfs, idx);
}
EXPORT_SYMBOL(ubase_dbg_seq_file_init);

static struct ubase_dbg_dentry_info ubase_dbg_dentry[] = {
	{
		.name = "qos",
		.property = UBASE_SUP_URMA | UBASE_SUP_CDMA | UBASE_SUP_UBL_ETH,
		.support = __ubase_dbg_dentry_support,
	},
	/* ue debugfs top-level directory,
	 * "dev_name" refers to the ue name
	 */
	{
		.name = "dev_name",
		.property = UBASE_SUP_URMA | UBASE_SUP_CDMA | UBASE_SUP_PMU |
			    UBASE_SUP_UBL_ETH,
		.support = __ubase_dbg_dentry_support,
	},
};

static struct ubase_dbg_cmd_info ubase_dbg_cmd[] = {
	{
		.name = "reset_info",
		.dentry_index = UBASE_DBG_DENTRY_ROOT,
		.property = UBASE_SUP_URMA | UBASE_SUP_CDMA | UBASE_SUP_PMU |
			    UBASE_SUP_UBL_ETH,
		.support = __ubase_dbg_dentry_support,
		.init = __ubase_dbg_seq_file_init,
		.read_func = ubase_dbg_dump_rst_info,
	},
	{
		.name = "sl_vl_map",
		.dentry_index = UBASE_DBG_DENTRY_QOS,
		.property = UBASE_SUP_URMA | UBASE_SUP_CDMA | UBASE_SUP_UBL_ETH,
		.support = __ubase_dbg_dentry_support,
		.init = __ubase_dbg_seq_file_init,
		.read_func = ubase_dbg_dump_sl_vl_map,
	},
	{
		.name = "udma_dscp_vl_map",
		.dentry_index = UBASE_DBG_DENTRY_QOS,
		.property = UBASE_SUP_URMA | UBASE_SUP_UBL_ETH,
		.support = __ubase_dbg_dentry_support,
		.init = __ubase_dbg_seq_file_init,
		.read_func = ubase_dbg_dump_udma_dscp_vl_map,
	},
	{
		.name = "ets_tc",
		.dentry_index = UBASE_DBG_DENTRY_QOS,
		.property = UBASE_SUP_URMA | UBASE_SUP_UBL_ETH,
		.support = __ubase_dbg_dentry_support,
		.init = __ubase_dbg_seq_file_init,
		.read_func = ubase_dbg_dump_ets_tc_info,
	},
	{
		.name = "ets_tcg",
		.dentry_index = UBASE_DBG_DENTRY_QOS,
		.property = UBASE_SUP_URMA | UBASE_SUP_UBL_ETH,
		.support = __ubase_dbg_dentry_support,
		.init = __ubase_dbg_seq_file_init,
		.read_func = ubase_dbg_dump_ets_tcg_info,
	},
	{
		.name = "ets_port",
		.dentry_index = UBASE_DBG_DENTRY_QOS,
		.property = UBASE_SUP_URMA | UBASE_SUP_UBL_ETH,
		.support = __ubase_dbg_dentry_support,
		.init = __ubase_dbg_seq_file_init,
		.read_func = ubase_dbg_dump_ets_port_info,
	},
	{
		.name = "rack_vl_bitmap",
		.dentry_index = UBASE_DBG_DENTRY_QOS,
		.property = UBASE_SUP_URMA | UBASE_SUP_CDMA | UBASE_SUP_UBL,
		.support = __ubase_dbg_dentry_support,
		.init = __ubase_dbg_seq_file_init,
		.read_func = ubase_dbg_dump_rack_vl_bitmap,
	},
	{
		.name = "adev_qos",
		.dentry_index = UBASE_DBG_DENTRY_QOS,
		.property = UBASE_SUP_URMA | UBASE_SUP_CDMA | UBASE_SUP_UBL_ETH,
		.support = __ubase_dbg_dentry_support,
		.init = __ubase_dbg_seq_file_init,
		.read_func = ubase_dbg_dump_adev_qos_info,
	},
	{
		.name = "tm_queue",
		.dentry_index = UBASE_DBG_DENTRY_QOS,
		.property = UBASE_SUP_URMA | UBASE_SUP_CDMA | UBASE_SUP_UBL_ETH,
		.support = __ubase_dbg_dentry_support,
		.init = __ubase_dbg_seq_file_init,
		.read_func = ubase_dbg_dump_tm_queue_info,
	},
	{
		.name = "tm_qset",
		.dentry_index = UBASE_DBG_DENTRY_QOS,
		.property = UBASE_SUP_URMA | UBASE_SUP_CDMA | UBASE_SUP_UBL_ETH,
		.support = __ubase_dbg_dentry_support,
		.init = __ubase_dbg_seq_file_init,
		.read_func = ubase_dbg_dump_tm_qset_info,
	},
	{
		.name = "tm_pri",
		.dentry_index = UBASE_DBG_DENTRY_QOS,
		.property = UBASE_SUP_URMA | UBASE_SUP_CDMA | UBASE_SUP_UBL_ETH,
		.support = __ubase_dbg_dentry_support,
		.init = __ubase_dbg_seq_file_init,
		.read_func = ubase_dbg_dump_tm_pri_info,
	},
	{
		.name = "tm_pg",
		.dentry_index = UBASE_DBG_DENTRY_QOS,
		.property = UBASE_SUP_URMA | UBASE_SUP_CDMA | UBASE_SUP_UBL_ETH,
		.support = __ubase_dbg_dentry_support,
		.init = __ubase_dbg_seq_file_init,
		.read_func = ubase_dbg_dump_tm_pg_info,
	},
	{
		.name = "tm_port",
		.dentry_index = UBASE_DBG_DENTRY_QOS,
		.property = UBASE_SUP_URMA | UBASE_SUP_CDMA | UBASE_SUP_UBL_ETH,
		.support = __ubase_dbg_dentry_support,
		.init = __ubase_dbg_seq_file_init,
		.read_func = ubase_dbg_dump_tm_port_info,
	},
};

static int ubase_dbg_create_dir(struct device *dev,
				struct ubase_dbg_dentry_info *dirs, u32 root_idx)
{
	u32 i;

	for (i = 0; i < root_idx; i++) {
		if (!dirs[i].support(dev, dirs[i].property))
			continue;

		dirs[i].dentry = debugfs_create_dir(dirs[i].name,
						    dirs[root_idx].dentry);
		if (IS_ERR(dirs[i].dentry)) {
			dev_err(dev, "failed to create %s dir.\n", dirs[i].name);
			return PTR_ERR(dirs[i].dentry);
		}
	}

	return 0;
}

static int ubase_dbg_create_file(struct device *dev, struct ubase_dbgfs *dbgfs,
				 struct ubase_dbg_dentry_info *dirs)
{
	struct ubase_dbg_cmd_info *cmd_info;
	int i, ret;

	cmd_info = dbgfs->cmd_info;
	for (i = 0; i < dbgfs->cmd_info_size; i++) {
		if (!cmd_info[i].support(dev, cmd_info[i].property))
			continue;

		ret = cmd_info[i].init(dev, dirs, dbgfs, i);
		if (ret) {
			dev_err(dev, "failed to init cmd %s, ret = %d.\n",
				cmd_info[i].name, ret);
			return ret;
		}
	}

	return 0;
}

int ubase_dbg_create_dentry(struct device *dev, struct ubase_dbgfs *dbgfs,
			    struct ubase_dbg_dentry_info *dirs, u32 root_idx)
{
	int ret;

	if (!dev || !dbgfs || !dirs || !dbgfs->cmd_info)
		return -EINVAL;

	ret = ubase_dbg_create_dir(dev, dirs, root_idx);
	if (ret) {
		dev_err(dev,
			"failed to create ubase debugfs dirs, ret = %d.\n", ret);
		return ret;
	}

	ret = ubase_dbg_create_file(dev, dbgfs, dirs);
	if (ret)
		dev_err(dev,
			"failed to create ubase debugfs files, ret = %d.\n", ret);

	return ret;
}
EXPORT_SYMBOL(ubase_dbg_create_dentry);

int ubase_dbg_init(struct ubase_dev *udev)
{
	struct ubase_dbg_dentry_info dentry[UBASE_DBG_DENTRY_ROOT + 1] = {0};
	const char *name = dev_name(udev->dev);
	struct device *dev = udev->dev;
	int ret;

	udev->dbgfs.dentry = debugfs_create_dir(name, ubase_dbgfs_root);
	if (IS_ERR(udev->dbgfs.dentry)) {
		ubase_err(udev, "failed to create ubase debugfs root dir.\n");
		return PTR_ERR(udev->dbgfs.dentry);
	}

	memcpy(dentry, ubase_dbg_dentry, sizeof(dentry));
	dentry[UBASE_DBG_DENTRY_ROOT].dentry = udev->dbgfs.dentry;
	udev->dbgfs.cmd_info = ubase_dbg_cmd;
	udev->dbgfs.cmd_info_size = ARRAY_SIZE(ubase_dbg_cmd);

	ret = ubase_dbg_create_dentry(dev, &udev->dbgfs, dentry,
				      ARRAY_SIZE(dentry) - 1);
	if (ret) {
		ubase_err(udev,
			  "failed to create ubase debugfs dentry, ret = %d.\n",
			  ret);
		goto create_dentry_err;
	}

	return 0;

create_dentry_err:
	debugfs_remove_recursive(udev->dbgfs.dentry);

	return ret;
}

void ubase_dbg_uninit(struct ubase_dev *udev)
{
	debugfs_remove_recursive(udev->dbgfs.dentry);
}

int ubase_dbg_register_debugfs(void)
{
#define UBASE_DBGFS_ROOT	"ubase"

	ubase_dbgfs_root = debugfs_create_dir(UBASE_DBGFS_ROOT, NULL);
	if (IS_ERR(ubase_dbgfs_root))
		return PTR_ERR(ubase_dbgfs_root);

	return 0;
}

void ubase_dbg_unregister_debugfs(void)
{
	debugfs_remove_recursive(ubase_dbgfs_root);
}

struct dentry *ubase_diag_debugfs_root(struct auxiliary_device *adev)
{
	if (!adev)
		return NULL;

	return __ubase_get_udev_by_adev(adev)->dbgfs.dentry;
}
EXPORT_SYMBOL(ubase_diag_debugfs_root);

int ubase_dbg_format_time(time64_t time, struct seq_file *s)
{
#define YEAR_OFFSET 1900
	const char week[7][4] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
	const char mouth[12][4] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
				   "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
	struct tm t;

	if (!s)
		return -EINVAL;

	time64_to_tm(time, 0, &t);

	seq_printf(s, "%s %s %02d %02d:%02d:%02d %ld", week[t.tm_wday],
		   mouth[t.tm_mon], t.tm_mday, t.tm_hour, t.tm_min,
		   t.tm_sec, t.tm_year + YEAR_OFFSET);
	return 0;
}
EXPORT_SYMBOL(ubase_dbg_format_time);

// SPDX-License-Identifier: GPL-2.0+
/* Copyright(c) 2025 HiSilicon Technologies CO., Ltd. All rights reserved. */

#define dev_fmt(fmt) "UDMA: " fmt
#define pr_fmt(fmt) "UDMA: " fmt

#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include "udma_cmd.h"
#include "udma_debugfs.h"

static struct dentry *g_udma_dbgfs_root;

static struct udma_debugfs_file_info g_ta_dfx_mod[TA_MAX_SIZE] = {
	{"mrd", RDONLY, UDMA_CMD_DEBUGFS_TA_INFO, UDMA_TA_MRD},
};

static struct udma_debugfs_file_info g_tp_dfx_mod[TP_MAX_SIZE] = {
	{"rxtx", RDONLY, UDMA_CMD_DEBUGFS_TP_INFO, UDMA_TP_RXTX},
};

static void show_ta_mrd_dfx(struct udma_query_mrd_dfx *data)
{
	pr_info("****************** ta_mrd_dfx ******************\n");
	pr_info("mrd_dsqe_issue_cnt\t0x%08x\n", data->mrd_dsqe_issue_cnt);
	pr_info("mrd_dsqe_exec_cnt\t0x%08x\n", data->mrd_dsqe_exec_cnt);
	pr_info("mrd_dsqe_drop_cnt\t0x%08x\n", data->mrd_dsqe_drop_cnt);
	pr_info("mrd_jfsdb_issue_cnt\t0x%08x\n", data->mrd_jfsdb_issue_cnt);
	pr_info("mrd_jfsdb_exec_cnt\t0x%08x\n", data->mrd_jfsdb_exec_cnt);
	pr_info("mrd_mb_issue_cnt\t\t0x%08x\n", data->mrd_mb_issue_cnt);
	pr_info("mrd_mb_exec_cnt\t\t0x%08x\n", data->mrd_mb_exec_cnt);
	pr_info("mrd_eqdb_issue_cnt\t0x%08x\n", data->mrd_eqdb_issue_cnt);
	pr_info("mrd_mb_buff_full\t\t0x%08x\n", data->mrd_mb_buff_full);
	pr_info("mrd_mb_buff_empty\t0x%08x\n", data->mrd_mb_buff_empty);
	pr_info("mrd_mem_ecc_err_1b\t0x%08x\n", data->mrd_mem_ecc_err_1b);
	pr_info("mrd_mem_ecc_1b_info\t0x%08x\n", data->mrd_mem_ecc_1b_info);
	pr_info("mrd_mb_state\t\t0x%08x\n", data->mrd_mb_state);
	pr_info("mrd_eqdb_exec_cnt\t0x%08x\n", data->mrd_eqdb_exec_cnt);
	pr_info("****************** ta_mrd_dfx ******************\n");
}

static void show_tp_rxtx_dfx(struct udma_query_rxtx_dfx *data)
{
	pr_info("****************** tp_rxtx_dfx ******************\n");
	pr_info("tpp2_txdma_hdr_um_pkt_cnt\t0x%016llx\n", data->tpp2_txdma_hdr_um_pkt_cnt);
	pr_info("tpp2_txdma_ctp_rm_pkt_cnt\t0x%016llx\n", data->tpp2_txdma_ctp_rm_pkt_cnt);
	pr_info("tpp2_txdma_ctp_rc_pkt_cnt\t0x%016llx\n", data->tpp2_txdma_ctp_rc_pkt_cnt);
	pr_info("tpp2_txdma_tp_rm_pkt_cnt\t\t0x%016llx\n", data->tpp2_txdma_tp_rm_pkt_cnt);
	pr_info("tpp2_txdma_tp_rc_pkt_cnt\t\t0x%016llx\n", data->tpp2_txdma_tp_rc_pkt_cnt);
	pr_info("rhp_glb_rm_pkt_cnt\t\t0x%016llx\n", data->rhp_glb_rm_pkt_cnt);
	pr_info("rhp_glb_rc_pkt_cnt\t\t0x%016llx\n", data->rhp_glb_rc_pkt_cnt);
	pr_info("rhp_clan_rm_pkt_cnt\t\t0x%016llx\n", data->rhp_clan_rm_pkt_cnt);
	pr_info("rhp_clan_rc_pkt_cnt\t\t0x%016llx\n", data->rhp_clan_rc_pkt_cnt);
	pr_info("rhp_ud_pkt_cnt\t\t\t0x%016llx\n", data->rhp_ud_pkt_cnt);
	pr_info("****************** tp_rxtx_dfx ******************\n");
}

static int udma_query_mrd_dfx(struct file_private_data *private_data)
{
	struct udma_query_mrd_dfx out_regs;
	struct ubase_cmd_buf in, out;
	int ret;

	out_regs.sub_module = private_data->sub_opcode;
	udma_fill_buf(&in, private_data->opcode, true,
		      sizeof(struct udma_query_mrd_dfx), &out_regs);
	udma_fill_buf(&out, private_data->opcode, true,
		      sizeof(struct udma_query_mrd_dfx), &out_regs);

	ret = ubase_cmd_send_inout(private_data->udma_dev->comdev.adev, &in, &out);
	if (ret) {
		dev_err(private_data->udma_dev->dev, "failed to query mrd DFX, ret = %d.\n", ret);
		return ret;
	}

	show_ta_mrd_dfx(&out_regs);

	return 0;
}

static int udma_query_rxtx_dfx(struct file_private_data *private_data)
{
	struct udma_query_rxtx_dfx out_regs;
	struct ubase_cmd_buf in, out;
	int ret;

	out_regs.sub_module = private_data->sub_opcode;
	udma_fill_buf(&in, private_data->opcode, true, sizeof(struct udma_query_rxtx_dfx),
		      &out_regs);
	udma_fill_buf(&out, private_data->opcode, true,
		      sizeof(struct udma_query_rxtx_dfx), &out_regs);

	ret = ubase_cmd_send_inout(private_data->udma_dev->comdev.adev, &in, &out);
	if (ret) {
		dev_err(private_data->udma_dev->dev, "failed to query rxtx DFX, ret = %d.\n", ret);
		return ret;
	}

	show_tp_rxtx_dfx(&out_regs);

	return 0;
}

static inline int udma_debugfs_open(struct inode *inode, struct file *filp)
{
	filp->private_data = inode->i_private;

	return 0;
}

static ssize_t udma_debugfs_read(struct file *filp, char __user *buf,
				 size_t size, loff_t *ppos)
{
	struct file_private_data *private_data = filp->private_data;
	int ret;

	switch (private_data->sub_opcode) {
	case UDMA_TA_MRD:
		ret = udma_query_mrd_dfx(private_data);
		break;
	case UDMA_TP_RXTX:
		ret = udma_query_rxtx_dfx(private_data);
		break;
	default:
		dev_err(private_data->udma_dev->dev, "invalid type %u.\n",
			private_data->sub_opcode);
		return -EFAULT;
	}

	return ret;
}

static const struct file_operations udma_debugfs_rd_fops = {
	.owner = THIS_MODULE,
	.read = udma_debugfs_read,
	.open = udma_debugfs_open,
};

static const uint16_t file_mod[FILE_MOD_SIZE] = {
	0200, 0400,
};

static int udma_debugfs_create_files(struct udma_dev *udma_dev, struct udma_dev_debugfs *dbgfs)
{
	struct file_private_data *private_data;
	struct file_private_data *cur_p;
	struct dentry *entry;
	int i;

	private_data = kzalloc(sizeof(struct file_private_data) * (TA_MAX_SIZE + TP_MAX_SIZE),
			       GFP_KERNEL);
	if (!private_data)
		return -ENOMEM;

	for (i = 0; i < TA_MAX_SIZE; ++i) {
		cur_p = private_data + i;
		cur_p->udma_dev = udma_dev;
		cur_p->opcode = g_ta_dfx_mod[i].opcode;
		cur_p->sub_opcode = g_ta_dfx_mod[i].sub_opcode;
		entry = debugfs_create_file(g_ta_dfx_mod[i].name, file_mod[g_ta_dfx_mod[i].fmod],
					    dbgfs->ta_root, cur_p, &udma_debugfs_rd_fops);
		if (IS_ERR(entry)) {
			dev_err(udma_dev->dev, "create %s failed.\n", g_ta_dfx_mod[i].name);
			kfree(private_data);
			return -EINVAL;
		}
	}

	for (i = 0; i < TP_MAX_SIZE; ++i) {
		cur_p = private_data + i + TA_MAX_SIZE;
		cur_p->udma_dev = udma_dev;
		cur_p->opcode = g_tp_dfx_mod[i].opcode;
		cur_p->sub_opcode = g_tp_dfx_mod[i].sub_opcode;
		entry = debugfs_create_file(g_tp_dfx_mod[i].name, file_mod[g_tp_dfx_mod[i].fmod],
					    dbgfs->tp_root, cur_p, &udma_debugfs_rd_fops);
		if (IS_ERR(entry)) {
			dev_err(udma_dev->dev, "create %s failed.\n", g_tp_dfx_mod[i].name);
			kfree(private_data);
			return -EINVAL;
		}
	}

	dbgfs->private_data = private_data;
	dbgfs->private_data_size = TA_MAX_SIZE + TP_MAX_SIZE;

	return 0;
}

void udma_register_debugfs(struct udma_dev *udma_dev)
{
	struct udma_dev_debugfs *dbgfs;

	if (IS_ERR_OR_NULL(g_udma_dbgfs_root)) {
		dev_err(udma_dev->dev, "Debugfs root path does not exist.\n");
		goto create_error;
	}

	dbgfs = kzalloc(sizeof(*dbgfs), GFP_KERNEL);
	if (!dbgfs)
		goto create_error;

	dbgfs->root = debugfs_create_dir(udma_dev->dev_name, g_udma_dbgfs_root);
	if (IS_ERR(dbgfs->root)) {
		dev_err(udma_dev->dev, "Debugfs create dev path failed.\n");
		goto create_dev_error;
	}

	dbgfs->ta_root = debugfs_create_dir("ta", dbgfs->root);
	if (IS_ERR(dbgfs->ta_root)) {
		dev_err(udma_dev->dev, "Debugfs create ta path failed.\n");
		goto create_path_error;
	}

	dbgfs->tp_root = debugfs_create_dir("tp", dbgfs->root);
	if (IS_ERR(dbgfs->tp_root)) {
		dev_err(udma_dev->dev, "Debugfs create tp path failed.\n");
		goto create_path_error;
	}

	if (udma_debugfs_create_files(udma_dev, dbgfs)) {
		dev_err(udma_dev->dev, "Debugfs create files failed.\n");
		goto create_path_error;
	}

	udma_dev->dbgfs = dbgfs;

	return;

create_path_error:
	debugfs_remove_recursive(dbgfs->root);
create_dev_error:
	kfree(dbgfs);
create_error:
	udma_dev->dbgfs = NULL;
}

void udma_unregister_debugfs(struct udma_dev *udma_dev)
{
	if (IS_ERR_OR_NULL(g_udma_dbgfs_root))
		return;

	if (!udma_dev->dbgfs)
		return;

	debugfs_remove_recursive(udma_dev->dbgfs->root);
	kfree(udma_dev->dbgfs->private_data);
	kfree(udma_dev->dbgfs);
	udma_dev->dbgfs = NULL;
}

void udma_init_debugfs(void)
{
	g_udma_dbgfs_root = debugfs_create_dir("udma", NULL);
}

void udma_uninit_debugfs(void)
{
	debugfs_remove_recursive(g_udma_dbgfs_root);
	g_udma_dbgfs_root = NULL;
}

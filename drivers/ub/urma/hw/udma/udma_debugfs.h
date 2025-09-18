/* SPDX-License-Identifier: GPL-2.0+ */
/* Copyright(c) 2025 HiSilicon Technologies CO., Ltd. All rights reserved. */

#ifndef __UDMA_DEBUGFS_H__
#define __UDMA_DEBUGFS_H__

#include "udma_dev.h"
#include "udma_cmd.h"

#define TA_MAX_SIZE 1
#define TP_MAX_SIZE 1
#define FILE_MOD_SIZE 2

enum udma_dfx_sub_opcode {
	UDMA_TA_MRD,
	UDMA_TP_RXTX,
};

enum udma_debugfs_file_mod {
	RDONLY,
};

struct udma_debugfs_file_info {
	const char *name;
	enum udma_debugfs_file_mod fmod;
	enum udma_cmd_opcode_type opcode;
	enum udma_dfx_sub_opcode sub_opcode;
};

struct udma_query_rxtx_dfx {
	uint32_t sub_module;
	uint64_t tpp2_txdma_hdr_um_pkt_cnt;
	uint64_t tpp2_txdma_ctp_rm_pkt_cnt;
	uint64_t tpp2_txdma_ctp_rc_pkt_cnt;
	uint64_t tpp2_txdma_tp_rm_pkt_cnt;
	uint64_t tpp2_txdma_tp_rc_pkt_cnt;
	uint64_t rhp_glb_rm_pkt_cnt;
	uint64_t rhp_glb_rc_pkt_cnt;
	uint64_t rhp_clan_rm_pkt_cnt;
	uint64_t rhp_clan_rc_pkt_cnt;
	uint64_t rhp_ud_pkt_cnt;
	uint32_t rsvd[16];
};

struct udma_query_mrd_dfx {
	uint32_t sub_module;
	uint32_t mrd_dsqe_issue_cnt;
	uint32_t mrd_dsqe_exec_cnt;
	uint32_t mrd_dsqe_drop_cnt;
	uint32_t mrd_jfsdb_issue_cnt;
	uint32_t mrd_jfsdb_exec_cnt;
	uint32_t mrd_mb_issue_cnt;
	uint32_t mrd_mb_exec_cnt;
	uint32_t mrd_eqdb_issue_cnt;
	uint32_t mrd_mb_buff_full;
	uint32_t mrd_mb_buff_empty;
	uint32_t mrd_mem_ecc_err_1b;
	uint32_t mrd_mem_ecc_1b_info;
	uint32_t mrd_mb_state;
	uint32_t mrd_eqdb_exec_cnt;
	uint32_t rsvd[7];
};

struct udma_dev_debugfs {
	struct dentry *root;
	struct dentry *ta_root;
	struct dentry *tp_root;
	struct file_private_data *private_data;
	uint32_t private_data_size;
};

struct file_private_data {
	struct udma_dev *udma_dev;
	enum udma_cmd_opcode_type opcode;
	enum udma_dfx_sub_opcode sub_opcode;
};

void udma_init_debugfs(void);
void udma_uninit_debugfs(void);
void udma_unregister_debugfs(struct udma_dev *udma_dev);
void udma_register_debugfs(struct udma_dev *udma_dev);

#endif /* __UDMA_DEBUGFS_H__ */

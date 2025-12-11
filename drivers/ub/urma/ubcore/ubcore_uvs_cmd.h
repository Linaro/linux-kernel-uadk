/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2025. All rights reserved.
 *
 * Description: ubcore uvs cmd header file
 * Author: Ji Lei
 * Create: 2023-07-03
 * Note:
 * History: 2023-07-03: Create file
 */

#ifndef UBCORE_UVS_CMD_H
#define UBCORE_UVS_CMD_H

#include <linux/types.h>
#include <linux/uaccess.h>
#include <ub/urma/ubcore_types.h>
#include "ubcore_cmd.h"
#include "ubcore_log.h"
#include "ubcore_priv.h"

#define UBCORE_UVS_CMD_MAGIC 'V'
#define UBCORE_UVS_CMD _IOWR(UBCORE_UVS_CMD_MAGIC, 1, struct ubcore_cmd_hdr)
#define UBCORE_CMD_CHANNEL_INIT_SIZE 32
#define UBCORE_MAX_VTP_CFG_CNT 32
#define UBCORE_MAX_EID_CONFIG_CNT 32
#define UBCORE_MAX_DSCP_VL_NUM 64
#define UBCORE_CMD_MAX_MUE_NUM 128

enum ubcore_uvs_global_cmd { UBCORE_CMD_SET_TOPO = 1, UBCORE_CMD_GLOBAL_LAST };

struct ubcore_cmd_set_topo {
	struct {
		void *topo_info;
		uint32_t topo_num;
	} in;
};

int ubcore_uvs_mue_cmd_parse(struct ubcore_mue_file *file,
			     struct ubcore_cmd_hdr *hdr);

int ubcore_uvs_global_cmd_parse(struct ubcore_global_file *file,
				struct ubcore_cmd_hdr *hdr);

#endif

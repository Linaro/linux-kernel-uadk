/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) HiSilicon Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef __HISI_UBUS_H__
#define __HISI_UBUS_H__

#include <ub/ubus/ubus.h>

#define MEM_INFO_NUM			5
#define MB_SIZE_OFFSET			20
#define HI_UBC_PRIVATE_DATA_RESERVED	3
#define HI_UBC_PRIVATE_DATA_RESERVED2	111

struct hi_mem_pa_info {
	u64 decode_addr;
	u32 cc_base_addr;
	u32 cc_base_size;
	u32 nc_base_addr;
	u32 nc_base_size;
};

struct hi_ubc_private_data {
	u32 ub_mem_version;
	u8 max_addr_bits;
	u8 reserved[HI_UBC_PRIVATE_DATA_RESERVED];
	struct hi_mem_pa_info mem_pa_info[MEM_INFO_NUM];
	u64 io_decoder_cmdq;
	u64 io_decoder_evtq;
	u8 features;
	u8 reserved2[HI_UBC_PRIVATE_DATA_RESERVED2];
};

int hi_eu_table_init(struct ub_bus_controller *ubc);
void hi_eu_table_uninit(struct ub_bus_controller *ubc);
int hi_eu_cfg(struct ub_bus_controller *ubc, bool add, u32 eid, u16 upi);
void hi_register_decoder_base_addr(struct ub_bus_controller *ubc, u64 *cmd_queue,
				   u64 *event_queue);
int hi_send_entity_enable_msg(struct ub_entity *uent, u8 enable);

int ub_bus_controller_probe(struct ub_bus_controller *ubc);
void ub_bus_controller_remove(struct ub_bus_controller *ubc);

#endif /* __HISI_UBUS_H__ */

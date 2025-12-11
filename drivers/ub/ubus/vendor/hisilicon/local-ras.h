/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) HiSilicon Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef __LOCAL_RAS_H__
#define __LOCAL_RAS_H__

#define HISI_UBUS_ERR_MISC_REGS 64

#define HISI_UBUS_LOCAL_VALID_SOC_ID BIT(0)
#define HISI_UBUS_LOCAL_VALID_SOCKET_ID BIT(1)
#define HISI_UBUS_LOCAL_VALID_TOTEM_ID BIT(2)
#define HISI_UBUS_LOCAL_VALID_NIMBUS_ID BIT(3)
#define HISI_UBUS_LOCAL_VALID_SUB_SYSTEM_ID BIT(4)
#define HISI_UBUS_LOCAL_VALID_MODULE_ID BIT(5)
#define HISI_UBUS_LOCAL_VALID_SUB_MODULE_ID BIT(6)
#define HISI_UBUS_LOCAL_VALID_CORE_ID BIT(7)
#define HISI_UBUS_LOCAL_VALID_PORT_ID BIT(8)
#define HISI_UBUS_LOCAL_VALID_ERR_TYPE BIT(9)
#define HISI_UBUS_LOCAL_VALID_ERR_INFO BIT(10)
#define HISI_UBUS_LOCAL_VALID_ERR_SEVERITY BIT(11)
#define HISI_UBUS_LOCAL_VALID_REG_SIZE BIT(12)

struct hisi_ubus_error_data {
	u32 val_bits;
	u8 version;
	u8 soc_id;
	u8 socket_id;
	u8 totem_id;
	u8 nimbus_id;
	u8 sub_system_id;
	u8 module_id;
	u8 sub_module_id;
	u8 core_id;
	u8 port_id;
	u16 err_type;
	u64 rsvd;
	u8 err_severity;
	u8 reserv[3];
	u32 register_array_size;
	u32 err_misc[];
};

struct hisi_ubus_error_private {
	struct notifier_block nb;
};

enum hisi_ubus_submodule_idx {
	HISI_UBUS_SUB_MODULE_ID_TP,
	HISI_UBUS_SUB_MODULE_ID_TA,
	HISI_UBUS_SUB_MODULE_ID_MISC,
	HISI_UBUS_SUB_MODULE_ID_BA,
	HISI_UBUS_SUB_MODULE_ID_NL,
	HISI_UBUS_SUB_MODULE_ID_DLMAC,
	HISI_UBUS_SUB_MODULE_ID_MUXPCS,
	HISI_UBUS_SUB_MODULE_ID_CCUM,
	HISI_UBUS_SUB_MODULE_ID_CCUA,
	HISI_UBUS_SUB_MODULE_ID_ETH_MAC,
	HISI_UBUS_SUB_MODULE_ID_INVAILD,
};

enum hisi_ubus_err_severity {
	HISI_UBUS_ERR_SEV_RECOVERABLE,
	HISI_UBUS_ERR_SEV_FATAL,
	HISI_UBUS_ERR_SEV_CORRECTED,
	HISI_UBUS_ERR_SEV_NONE,
};

void ub_ras_handler_remove(void);
int ub_ras_handler_probe(void);

#endif /* __LOCAL_RAS_H__ */

/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) HiSilicon Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef __SERVICE_RAS_H__
#define __SERVICE_RAS_H__

#define RAS_SPEC_ERR_PKG_LEN 32

#pragma pack(4)
struct ras_cap_regs {
	u32 err_ctrl_reg;
	u64 uncor_status;
	u64 uncor_mask;
	u64 uncor_severity;
	u64 cor_status;
	u64 cor_mask;
	u64 cor_severity;
	u32 err_info_ctrl_reg;
	u32 spec_def_err_info[RAS_SPEC_ERR_PKG_LEN];
	u32 vendor_def_err_info[2];
};
#pragma pack()

struct ras_recover_entry {
	u32 eid;
	u32 cna;
	u32 port;
	u32 vendor;
	u16 device;
	u16 type;
	u16 class_code;
	u16 err_level;
	int severity;
	u32 overflow;
	struct ras_cap_regs *regs;
};

#endif /* __SERVICE_RAS_H__ */

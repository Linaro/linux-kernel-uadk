/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) HiSilicon Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef __UB_FI_H__
#define __UB_FI_H__

enum bios_report_mode {
	ACPI = 0,
	DTS = 1,
	UBIOS = 3,
	UNKNOWN = 4,
};

extern enum bios_report_mode bios_mode;
#endif /* __UB_FI_H__ */

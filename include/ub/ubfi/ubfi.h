/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) HiSilicon Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef _UB_UBFI_UBFI_H_
#define _UB_UBFI_UBFI_H_

#include <linux/device.h>
#include <linux/ioport.h>
#include <linux/irqdomain_defs.h>
#include <linux/list.h>
#include <linux/types.h>

/**
 * ubrt_register_gsi() - Registering UBC's interrupt into the kernel via ACPI
 * @hwirq: GSI IRQ number reported by BIOS
 * @trigger: trigger type of the GSI number to be mapped
 * @polarity: polarity of the GSI to be mapped
 * @name: GSI IRQ name
 * @res: Record the soft interrupt number obtained after registration into res
 *
 * Return: 0 if success or other if failed
 */
int ubrt_register_gsi(u32 hwirq, int trigger, int polarity, const char *name,
		      struct resource *res);

/**
 * ubrt_unregister_gsi() - Unregistering UBC's interrupt into the kernel via ACPI
 * @hwirq: GSI IRQ number reported by BIOS
 */
void ubrt_unregister_gsi(u32 hwirq);

#if IS_ENABLED(CONFIG_UB_UBFI)
extern struct list_head ubc_list;
extern u32 ubc_eid_start;
extern u32 ubc_eid_end;
extern u32 ubc_cna_start;
extern u32 ubc_cna_end;
extern u8 ubc_feature;
#endif /* CONFIG_UB_UBFI */

#endif /* _UB_UBFI_UBFI_H_ */

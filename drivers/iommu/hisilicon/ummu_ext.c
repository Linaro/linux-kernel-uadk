// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved.
 * Description: HISI UMMU private extensions for the UMMU Framework.
 */

#define pr_fmt(fmt) "[UMMU][HISI_PRIV]: " fmt
#include <linux/module.h>
#include <linux/sizes.h>

#include "ummu.h"

/* Indicates whether to allow permission table space expansion. */
static bool ummu_mapt_blk_exp __read_mostly = true;

/* the basic block's size of the permission table is 64KB */
static size_t ummu_mapt_base_blk_size __read_mostly = SZ_64K;

static int __init ummu_mapt_blk_expan_setup(char *str)
{
	int ret = kstrtobool(str, &ummu_mapt_blk_exp);

	if (!ret)
		pr_info("the expansion capability of the ummu mapt block is %s.\n",
			ummu_mapt_blk_exp ? "on" : "off");
	return ret;
}
early_param("ummu.mapt_exp", ummu_mapt_blk_expan_setup);

bool ummu_get_mapt_blk_exp(void)
{
	return ummu_mapt_blk_exp;
}
EXPORT_SYMBOL_NS_GPL(ummu_get_mapt_blk_exp, UMMU_INTERNAL);

static int __init ummu_mapt_base_blk_size_setup(char *p)
{
	ummu_mapt_base_blk_size = memparse(p, &p);
	if (ummu_mapt_base_blk_size == 0 || ummu_mapt_base_blk_size > SZ_2M)
		ummu_mapt_base_blk_size = SZ_64K;
	pr_info("the ummu mapt block expansion size 0x%lx.\n",
		ummu_mapt_base_blk_size);
	return 0;
}
early_param("ummu.mapt_base_size", ummu_mapt_base_blk_size_setup);

size_t ummu_get_mapt_base_blk_size(void)
{
	return ummu_mapt_base_blk_size;
}
EXPORT_SYMBOL_NS_GPL(ummu_get_mapt_base_blk_size, UMMU_INTERNAL);

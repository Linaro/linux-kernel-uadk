// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) HiSilicon Technologies Co., Ltd. 2025. All rights reserved.
 */

#define pr_fmt(fmt)	"ubus cc: " fmt

#include "ubus.h"

static int ub_cc_safe_check(struct ub_entity *uent)
{
	if (!uent)
		return -EINVAL;

	if (is_idev(uent)) {
		if (!uent->ubc || !uent->ubc->uent)
			return -EINVAL;
	}

	return 0;
}

bool ub_cc_supported(struct ub_entity *uent)
{
	if (ub_cc_safe_check(uent))
		return false;

	if (is_idev(uent)) {
		/* Now, idev cc enable depends ub bus controller cc enable */
		if ((uent->ubc->uent->support_feature & UB_CC_SUPPORT) &&
		    (uent->support_feature & UB_CC_SUPPORT))
			return true;
	} else {
		if (uent->support_feature & UB_CC_SUPPORT)
			return true;
	}

	return false;
}
EXPORT_SYMBOL_GPL(ub_cc_supported);

/**
 * ub_cc_set - Set congestion control for given ub entity
 * @uent: ub entity
 * @val: cc switch
 *
 * Returns: 0 on success, an error otherwise.
 */
static int ub_cc_set(struct ub_entity *uent, u8 val)
{
	if (!ub_cc_supported(uent))
		return -EPERM;

	return ub_cfg_write_byte(uent, UB_CC_EN, val);
}

/**
 * ub_cc_enable - Enable congestion control for given ub entity
 * @uent: ub entity
 *
 * Returns: 0 on success, an error otherwise.
 */
int ub_cc_enable(struct ub_entity *uent)
{
	if (ub_cc_safe_check(uent))
		return -EINVAL;

	return ub_cc_set(uent, 1);
}
EXPORT_SYMBOL_GPL(ub_cc_enable);

/**
 * ub_cc_disable - Disable congestion control for given ub entity
 * @uent: ub entity
 *
 * Returns: 0 on success, an error otherwise.
 */
int ub_cc_disable(struct ub_entity *uent)
{
	if (ub_cc_safe_check(uent))
		return -EINVAL;

	return ub_cc_set(uent, 0);
}
EXPORT_SYMBOL_GPL(ub_cc_disable);

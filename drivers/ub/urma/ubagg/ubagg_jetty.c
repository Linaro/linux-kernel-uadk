// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 *
 * Description: ubagg jetty ops implementation
 * Author: Wang Hang
 * Create: 2025-08-13
 * Note:
 * History: 2025-08-13: Create file
 */

#include "ubagg_jetty.h"
#include "ubagg_log.h"

struct ubagg_target_jetty {
	struct ubcore_tjetty base;
};

struct ubcore_tjetty *ubagg_import_jfr(struct ubcore_device *dev,
				       struct ubcore_tjetty_cfg *cfg,
				       struct ubcore_udata *udata)
{
	struct ubagg_target_jetty *tjfr;

	if (dev == NULL || cfg == NULL || udata == NULL || udata->uctx == NULL)
		return NULL;

	tjfr = kzalloc(sizeof(struct ubagg_target_jetty), GFP_KERNEL);
	if (tjfr == NULL)
		return NULL;
	ubagg_log_info("Import jfr successfully, is:%u.\n", cfg->id.id);
	return &tjfr->base;
}

int ubagg_unimport_jfr(struct ubcore_tjetty *tjfr)
{
	struct ubagg_target_jetty *ubagg_tjfr;

	if (tjfr == NULL || tjfr->ub_dev == NULL || tjfr->uctx == NULL) {
		ubagg_log_err("Invalid parameter.\n");
		return -EINVAL;
	}
	ubagg_tjfr = (struct ubagg_target_jetty *)tjfr;
	ubagg_log_info("Unimport jfr successfully, id:%u.\n",
		       ubagg_tjfr->base.cfg.id.id);
	kfree(ubagg_tjfr);
	return 0;
}

struct ubcore_tjetty *ubagg_import_jetty(struct ubcore_device *dev,
					 struct ubcore_tjetty_cfg *cfg,
					 struct ubcore_udata *udata)
{
	struct ubagg_target_jetty *tjetty;

	if (cfg == NULL || dev == NULL || udata == NULL)
		return NULL;

	tjetty = kzalloc(sizeof(struct ubagg_target_jetty), GFP_KERNEL);
	if (tjetty == NULL)
		return NULL;
	ubagg_log_info("Import jetty successfully, %u\n", cfg->id.id);
	return &tjetty->base;
}

int ubagg_unimport_jetty(struct ubcore_tjetty *tjetty)
{
	struct ubagg_target_jetty *ubagg_tjetty;

	if (tjetty == NULL || tjetty->ub_dev == NULL || tjetty->uctx == NULL)
		return -EINVAL;
	ubagg_tjetty = (struct ubagg_target_jetty *)tjetty;
	ubagg_log_info("Unimport jetty successfully, id:%u.\n",
		       tjetty->cfg.id.id);
	kfree(ubagg_tjetty);
	return 0;
}

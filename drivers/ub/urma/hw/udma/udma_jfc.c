// SPDX-License-Identifier: GPL-2.0+
/* Copyright(c) 2025 HiSilicon Technologies CO., Ltd. All rights reserved. */

#define dev_fmt(fmt) "UDMA: " fmt

#include <linux/slab.h>
#include <linux/dma-mapping.h>
#include <linux/ummu_core.h>
#include <ub/urma/ubcore_uapi.h>
#include <uapi/ub/urma/udma/udma_abi.h>
#include "udma_cmd.h"
#include "udma_common.h"
#include "udma_jetty.h"
#include "udma_jfr.h"
#include "udma_jfs.h"
#include "udma_ctx.h"
#include "udma_db.h"
#include <ub/urma/udma/udma_ctl.h>
#include "udma_jfc.h"

int udma_jfc_completion(struct notifier_block *nb, unsigned long jfcn,
			void *data)
{
	struct auxiliary_device *adev = (struct auxiliary_device *)data;
	struct udma_dev *udma_dev = get_udma_dev(adev);
	struct ubcore_jfc *ubcore_jfc;
	struct udma_jfc *udma_jfc;

	xa_lock(&udma_dev->jfc_table.xa);
	udma_jfc = (struct udma_jfc *)xa_load(&udma_dev->jfc_table.xa, jfcn);
	if (!udma_jfc) {
		dev_warn(udma_dev->dev,
			 "Completion event for bogus jfcn %lu.\n", jfcn);
		xa_unlock(&udma_dev->jfc_table.xa);
		return -EINVAL;
	}

	++udma_jfc->arm_sn;

	ubcore_jfc = &udma_jfc->base;
	if (ubcore_jfc->jfce_handler) {
		refcount_inc(&udma_jfc->event_refcount);
		xa_unlock(&udma_dev->jfc_table.xa);
		ubcore_jfc->jfce_handler(ubcore_jfc);
		if (refcount_dec_and_test(&udma_jfc->event_refcount))
			complete(&udma_jfc->event_comp);
	} else {
		xa_unlock(&udma_dev->jfc_table.xa);
	}

	return 0;
}

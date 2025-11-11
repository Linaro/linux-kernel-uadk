// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2021-2025. All rights reserved.
 *
 * Description: ubcore jetty kernel module
 * Author: Ouyang Changchun
 * Create: 2021-11-25
 * Note:
 * History: 2021-11-25: create file
 * History: 2022-07-28: Yan Fangfang move jetty implementation here
 */

#include "ubcore_tp_table.h"

static void ubcore_jfs_kref_release(struct kref *ref_cnt)
{
	struct ubcore_jfs *jfs =
		container_of(ref_cnt, struct ubcore_jfs, ref_cnt);

	complete(&jfs->comp);
}

void ubcore_put_jfs(struct ubcore_jfs *jfs)
{
	(void)kref_put(&jfs->ref_cnt, ubcore_jfs_kref_release);
}

void ubcore_jfs_get(void *obj)
{
	struct ubcore_jfs *jfs = obj;

	kref_get(&jfs->ref_cnt);
}

static void ubcore_jfr_kref_release(struct kref *ref_cnt)
{
	struct ubcore_jfr *jfr =
		container_of(ref_cnt, struct ubcore_jfr, ref_cnt);

	complete(&jfr->comp);
}

void ubcore_put_jfr(struct ubcore_jfr *jfr)
{
	(void)kref_put(&jfr->ref_cnt, ubcore_jfr_kref_release);
}

void ubcore_jfr_get(void *obj)
{
	struct ubcore_jfr *jfr = obj;

	kref_get(&jfr->ref_cnt);
}

static void ubcore_jetty_kref_release(struct kref *ref_cnt)
{
	struct ubcore_jetty *jetty =
		container_of(ref_cnt, struct ubcore_jetty, ref_cnt);

	complete(&jetty->comp);
}

void ubcore_put_jetty(struct ubcore_jetty *jetty)
{
	(void)kref_put(&jetty->ref_cnt, ubcore_jetty_kref_release);
}

void ubcore_jetty_get(void *obj)
{
	struct ubcore_jetty *jetty = obj;

	kref_get(&jetty->ref_cnt);
}

struct ubcore_jfc *ubcore_find_jfc(struct ubcore_device *dev, uint32_t jfc_id)
{
	if (dev == NULL) {
		ubcore_log_err("dev is NULL\n");
		return NULL;
	}
	return ubcore_hash_table_lookup(&dev->ht[UBCORE_HT_JFC], jfc_id,
					&jfc_id);
}
EXPORT_SYMBOL(ubcore_find_jfc);

struct ubcore_jfs *ubcore_find_jfs(struct ubcore_device *dev, uint32_t jfs_id)
{
	if (!dev) {
		ubcore_log_err("dev is NULL\n");
		return NULL;
	}
	return ubcore_hash_table_lookup(&dev->ht[UBCORE_HT_JFS], jfs_id,
					&jfs_id);
}
EXPORT_SYMBOL(ubcore_find_jfs);

struct ubcore_jfr *ubcore_find_jfr(struct ubcore_device *dev, uint32_t jfr_id)
{
	if (!dev) {
		ubcore_log_err("dev is NULL\n");
		return NULL;
	}
	return ubcore_hash_table_lookup(&dev->ht[UBCORE_HT_JFR], jfr_id,
					&jfr_id);
}
EXPORT_SYMBOL(ubcore_find_jfr);

struct ubcore_jetty *ubcore_find_jetty(struct ubcore_device *dev,
				       uint32_t jetty_id)
{
	if (!dev) {
		ubcore_log_err("invalid parameter.\n");
		return NULL;
	}

	return ubcore_hash_table_lookup(&dev->ht[UBCORE_HT_JETTY], jetty_id,
					&jetty_id);
}
EXPORT_SYMBOL(ubcore_find_jetty);


// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) HiSilicon Technologies Co., Ltd. 2025. All rights reserved.
 */

#define pr_fmt(fmt) "ubus instance: " fmt

#include <ub/ubfi/ubfi.h>
#include <uapi/ub/ubus/ubus.h>

#include "ubus.h"
#include "eid.h"
#include "eu.h"
#include "ubus_driver.h"
#include "instance.h"

static LIST_HEAD(ubi_list);
static DEFINE_MUTEX(ubi_list_mutex);
static u32 instance_count;

static void ub_unregister_bus_instance(struct ub_bus_instance *bi);
static void ub_bus_instance_destroy(struct ub_bus_instance *bi)
{
	guid_t *tar = (guid_t *)&guid_null;

	ummu_core_del_eid(tar, bi->info.eid, EID_BYPASS);
	ub_unregister_bus_instance(bi);
}

static void ub_release_bus_instance(struct kref *kref)
{
	struct ub_bus_instance *bi =
		container_of(kref, struct ub_bus_instance, kref);

	if (bi->registered)
		ub_bus_instance_destroy(bi);

	kfree(bi);
}

static struct ub_bus_instance *ub_bus_instance_get(struct ub_bus_instance *bi)
{
	if (bi)
		kref_get(&bi->kref);

	return bi;
}

void ub_bus_instance_put(struct ub_bus_instance *bi)
{
	if (bi)
		kref_put(&bi->kref, ub_release_bus_instance);
}
EXPORT_SYMBOL_GPL(ub_bus_instance_put);

static struct ub_bus_instance *ub_alloc_bus_instance(void)
{
	struct ub_bus_instance *bi;

	bi = kzalloc(sizeof(*bi), GFP_KERNEL);
	if (!bi)
		return NULL;

	INIT_LIST_HEAD(&bi->node);
	kref_init(&bi->kref);
	INIT_LIST_HEAD(&bi->uents);
	mutex_init(&bi->lock);

	return bi;
}

static bool guid_match(struct ub_bus_instance *bi, void *arg)
{
	guid_t *guid = (guid_t *)arg;

	return guid_equal(&bi->info.guid.id, guid);
}

struct ub_bus_instance *ub_find_bus_instance(instance_match match, void *arg)
{
	struct ub_bus_instance *bi;

	if (!match || !arg) {
		pr_err("instance match or arg is null\n");
		return NULL;
	}

	mutex_lock(&ubi_list_mutex);
	list_for_each_entry(bi, &ubi_list, node)
		if (match(bi, arg))
			goto out;

	bi = NULL;
out:
	ub_bus_instance_get(bi);
	mutex_unlock(&ubi_list_mutex);

	return bi;
}
EXPORT_SYMBOL_GPL(ub_find_bus_instance);

static int bi_duplicate_check(struct ub_bus_instance_info *n)
{
	struct ub_bus_instance_info *old;
	struct ub_bus_instance *bi;
	int ret;

	bi = ub_find_bus_instance(guid_match, &n->guid.id);
	if (!bi)
		return 0;

	old = &bi->info;

	if (n->type != old->type) {
		pr_err("bus instance guid exist with different type\n");
		ub_bus_instance_put(bi);
		return -EINVAL;
	}

	if (n->eid) {
		if (n->upi)
			ret = (n->eid == old->eid && n->upi == old->upi) ?
				      -EEXIST :
				      -EINVAL;
		else
			ret = (n->eid == old->eid) ? -EEXIST : -EINVAL;
	} else {
		if (n->upi)
			ret = (n->upi == old->upi) ? -EEXIST : -EINVAL;
		else
			ret = -EEXIST;
	}

	if (ret == -EEXIST)
		pr_warn("bus instance guid exist\n");
	else
		pr_err("bus instance guid exist, but eid/upi invalid\n");

	ub_bus_instance_put(bi);

	return ret;
}

static int bi_valid_check(struct ub_bus_instance *bi)
{
	struct ub_bus_instance_info *info = &bi->info;

	if (info->guid.bits.type != UB_TYPE_BUS_INSTANCE ||
	    guid_is_null((const guid_t *)&info->guid)) {
		pr_err("guid type is not bus instance or guid_null\n");
		return -EINVAL;
	}

	if (is_server(bi) && info->eid && info->eid <= ubc_eid_end) {
		pr_err("server bi eid within the local scope, eid=%#x\n",
		       info->eid);
		return -EINVAL;
	}

	return 0;
}

static int ub_cfg_bus_instance_eid(struct ub_bus_instance *bi, bool alloc)
{
	struct ub_bus_instance_info *info = &bi->info;
	struct ub_guid guid = info->guid;
	u32 eid;
	int ret;

	if (alloc) {
		if (info->eid)
			return 0;

		ret = ub_eid_request(&guid.id, &eid);
		if (ret) {
			pr_err("bus instance eid cfg failed, ret=%d\n", ret);
			return ret;
		}

		info->eid = eid;
	} else {
		if (info->eid > ubc_eid_end)
			return 0;

		ub_eid_release(info->eid);
		info->eid = 0;
	}

	return 0;
}

static void ub_cfg_bus_instance_upi(struct ub_bus_instance *bi)
{
	struct ub_bus_instance_info *info = &bi->info;

	if (info->upi)
		return;

	info->upi = UB_CP_UPI;
}

static int ub_register_bus_instance(struct ub_bus_instance *bi)
{
	struct ub_bus_instance_info *info = &bi->info;
	int ret;

	ret = bi_duplicate_check(info);
	if (ret)
		return ret;

	ret = bi_valid_check(bi);
	if (ret)
		return ret;

	ret = ub_cfg_bus_instance_eid(bi, true);
	if (ret)
		return ret;

	ub_cfg_bus_instance_upi(bi);

	ret = ub_cfg_eu_table(bi->major, true, bi->info.eid,
				bi->info.upi);
	if (ret)
		goto out;

	mutex_lock(&ubi_list_mutex);
	bi->registered = true;
	list_add_tail(&bi->node, &ubi_list);
	instance_count++;
	mutex_unlock(&ubi_list_mutex);
	return 0;

out:
	/* upi no need unconfigure */
	(void)ub_cfg_bus_instance_eid(bi, false);
	return ret;
}

static void ub_unregister_bus_instance(struct ub_bus_instance *bi)
{
	mutex_lock(&ubi_list_mutex);
	if (!bi->registered) {
		mutex_unlock(&ubi_list_mutex);
		return;
	}

	instance_count--;
	list_del(&bi->node);
	bi->registered = false;
	mutex_unlock(&ubi_list_mutex);

	(void)ub_cfg_eu_table(bi->major, false, bi->info.eid,
				bi->info.upi);

	/* upi no need unconfigure */
	(void)ub_cfg_bus_instance_eid(bi, false);
}

int ub_static_bus_instance_init(struct ub_bus_controller *ubc)
{
	struct ub_bus_instance *bi;
	struct ub_bus_controller *tmp;
	int ret;

	if (ubc->ctl_no != 0) {
		tmp = ub_find_bus_controller(0);
		if (!tmp || !tmp->bi)
			return -EINVAL;

		ubc->bi = ub_bus_instance_get(tmp->bi);

		return ub_cfg_eu_table(ubc, true, ubc->bi->info.eid,
				      ubc->bi->info.upi);
	}

	bi = ub_alloc_bus_instance();
	if (!bi)
		return -ENOMEM;

	bi->info.type = UBUS_INSTANCE_STATIC_SERVER;
	guid_copy(&bi->info.guid.id, &ubc->uent->guid.id);
	bi->info.guid.bits.type = UB_TYPE_BUS_INSTANCE;
	bi->major = ubc;

	ret = ub_register_bus_instance(bi);
	if (ret) {
		dev_err(&ubc->dev, "static server register bi failed ret=%d\n",
			ret);
		goto put;
	}

	ret = ummu_core_add_eid((guid_t *)&guid_null, bi->info.eid,
				EID_BYPASS);
	if (ret) {
		dev_err(&ubc->dev, "static server ummu core add eid, ret=%d\n",
			ret);
		goto unregister;
	}

	ubc->bi = ub_bus_instance_get(bi);

	ub_bus_instance_put(bi); /* put the init one */

	return 0;
unregister:
	ub_unregister_bus_instance(bi);
put:
	ub_bus_instance_put(bi);
	return ret;
}

void ub_static_bus_instance_uninit(struct ub_bus_controller *ubc)
{
	if (ubc->ctl_no != 0)
		(void)ub_cfg_eu_table(ubc, false, ubc->bi->info.eid,
				      ubc->bi->info.upi);

	ub_bus_instance_put(ubc->bi);
	ubc->bi = NULL;
}

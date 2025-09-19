// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) HiSilicon Technologies Co., Ltd. 2025. All rights reserved.
 */

#define pr_fmt(fmt)	"ubus entity: " fmt

#include <linux/dma-mapping.h>

#include "ubus.h"
#include "sysfs.h"
#include "msg.h"
#include "enum.h"
#include "route.h"
#include "port.h"
#include "eid.h"
#include "cna.h"
#include "resource.h"
#include "ubus_controller.h"
#include "ubus_driver.h"
#include "ubus_inner.h"

/*
 * Entity lifecycle
 *
 * init process: {ub_alloc_ent -> ub_setup_ent -> ub_entity_add -> ub_start_ent}
 *
 * uninit process: {ub_stop_ent -> ub_remove_ent}
 */

#define UENT_NUM_START 1
#define UENT_NUM_END 0xfffff
#define UB_DEFAULT_MAX_SEG_SIZE SZ_64K

struct ub_entity *ub_alloc_ent(void)
{
	struct ub_entity *uent;

	uent = kzalloc(sizeof(*uent), GFP_KERNEL);
	if (!uent)
		return NULL;

	INIT_LIST_HEAD(&uent->node);
	INIT_LIST_HEAD(&uent->cna_list);

	uent->dev.type = &ub_dev_type;
	uent->cna = 0;

	ub_entity_assign_priv_flag(uent, UB_ENTITY_DETACHED, true);

	return uent;
}
EXPORT_SYMBOL_GPL(ub_alloc_ent);

static DEFINE_IDA(uent_num_ida);
void ub_entity_num_free(struct ub_entity *uent)
{
	ida_free(&uent_num_ida, uent->uent_num);
}

static int ub_entity_num_alloc(void)
{
	return ida_alloc_range(&uent_num_ida, UENT_NUM_START, UENT_NUM_END,
			       GFP_KERNEL);
}

static int ub_get_guid(struct ub_entity *uent)
{
	return 0;
}

static void ub_config_upi(struct ub_entity *uent)
{
	struct device *dev;
	int ret;
	u16 upi;

	if (is_ibus_controller(uent) && uent->ubc->cluster) {
		dev = &uent->ubc->dev;
		ret = ub_cfg_read_word(uent, UB_UPI, &upi);
		if (ret) {
			dev_err(dev, "update cluster upi failed, ret=%d\n", ret);
			return;
		}

		upi &= UB_UPI_MASK;
		if (upi) {
			dev_info(dev, "update cluster ubc upi, upi=%#x\n", upi);
			uent->upi = upi;
		}
		return;
	}

	ret = ub_cfg_write_dword(uent, UB_UPI, uent->upi);
	if (ret)
		ub_err(uent, "cfg upi failed, ret=%d\n", ret);
}

static int ub_setup_ent_primary(struct ub_entity *uent)
{
	return -EINVAL;
}

static int ub_uent_cfg(struct ub_entity *uent, u32 uent_num)
{
	struct ub_guid *guid = &uent->guid;
	char buf[SZ_64] = {};

	dev_set_name(&uent->dev, "%05x", uent_num);
	uent->uent_num = uent_num;

	(void)ub_show_guid(guid, buf);
	ub_info(uent, "guid=%s, uent_num=%#05x\n", buf, uent_num);

	uent->dev.bus = &ub_bus_type;
	/* Card driver set to 64bit if support */
	uent->dma_mask = GENMASK(31, 0);

	return ub_setup_ent_primary(uent);
}

static void ub_config_eid(struct ub_entity *uent)
{
	if (is_ibus_controller(uent) && uent->ubc->cluster)
		return;

	ub_cfg_write_dword(uent, UB_EID_0, uent->eid);
}

static void ub_set_fm_info(struct ub_entity *uent)
{
	if (uent->entity_idx)
		return;

	if (uent->ubc->cluster && !is_idev(uent))
		return;

	ub_cfg_write_dword(uent, UB_FM_CNA, uent->ubc->uent->cna);
	ub_cfg_write_dword(uent, UB_FM_EID_0, uent->ubc->uent->eid);
}

static void ub_get_module_id(struct ub_entity *uent)
{
	int ret;
	u32 val;

	ret = ub_cfg_read_dword(uent, UB_MODULE, &val);
	if (ret) {
		ub_err(uent, "Get dev module failed %d\n", ret);
		return;
	}

	uent->mod_vendor = val >> SZ_16;
	uent->module = val & UB_MODULE_ID_MASK;
}

int ub_setup_ent(struct ub_entity *uent)
{
	int ret, uent_num;

	if (!uent)
		return -EINVAL;

	ret = message_probe_device(uent);
	if (ret) {
		ub_err(uent, "probe message failed, ret=%d\n", ret);
		return ret;
	}

	ret = ub_get_guid(uent);
	if (ret) {
		ub_err(uent, "get guid failed, ret=%d\n", ret);
		goto err_alloc;
	}

	ub_config_upi(uent);

	/* common setup */
	ret = ub_eid_alloc(uent);
	if (ret) {
		ub_err(uent, "alloc eid failed, ret=%d\n", ret);
		goto err_alloc;
	}

	uent_num = ub_entity_num_alloc();
	if (uent_num < 0) {
		ub_err(uent, "alloc dev uent_num failed, ret=%d\n", uent_num);
		goto free_eid;
	}

	ret = ub_uent_cfg(uent, (u32)uent_num);
	if (ret)
		goto free_uent_num;

	ub_config_eid(uent);
	ub_set_fm_info(uent);
	ub_get_module_id(uent);

	ub_entity_assign_priv_flag(uent, UB_ENTITY_SETUP, true);
	return 0;
free_uent_num:
	ub_entity_num_free(uent);
free_eid:
	ub_eid_free(uent);
err_alloc:
	message_remove_device(uent);
	return ret;
}
EXPORT_SYMBOL_GPL(ub_setup_ent);

static void ub_configure_ent(struct ub_entity *uent)
{
}

static void ub_unconfigure_ent(struct ub_entity *uent)
{
}

static void ub_release_ent(struct device *dev);
void ub_entity_add(struct ub_entity *uent, void *ctx)
{
	struct ub_bus_controller *ubc;
	struct list_head *list;
	int ret, node;

	if (!uent || !ctx)
		return;

	ret = ub_entity_setup_mmio(uent);
	WARN_ON(ret);

	ub_configure_ent(uent);

	device_initialize(&uent->dev);
	uent->dev.release = ub_release_ent;

	uent->dev.dma_mask = &uent->dma_mask;
	uent->dev.dma_parms = &uent->dma_parms;
	uent->dev.coherent_dma_mask = GENMASK_ULL(31, 0);
	dma_set_max_seg_size(&uent->dev, UB_DEFAULT_MAX_SEG_SIZE);
	dma_set_seg_boundary(&uent->dev, GENMASK(31, 0));

	ubc = (struct ub_bus_controller *)ctx;
	node = ub_ubc_to_node(ubc);
	list = &ubc->devs;

	set_dev_node(&uent->dev, node);
	ub_entity_assign_priv_flag(uent, UB_ENTITY_DETACHED, false);

	down_write(&ub_bus_sem);
	list_add_tail(&uent->node, list);
	up_write(&ub_bus_sem);

	uent->match_driver = false;
	ret = device_add(&uent->dev);
	WARN_ON(ret < 0);

	ret = ub_ports_add(uent);
	WARN_ON(ret);
}
EXPORT_SYMBOL_GPL(ub_entity_add);

void ub_start_ent(struct ub_entity *uent)
{
	int ret;

	if (!uent)
		return;

	uent->match_driver = true;
	ret = device_attach(&uent->dev);
	if (ret < 0 && ret != -EPROBE_DEFER)
		ub_err(uent, "device attach failed, ret=%d\n", ret);

	ub_entity_assign_priv_flag(uent, UB_ENTITY_START, true);
}
EXPORT_SYMBOL_GPL(ub_start_ent);

static void ub_release_ent(struct device *dev)
{
	struct ub_entity *uent;

	uent = to_ub_entity(dev);

	ub_route_clear(uent);
	ub_cna_free(uent);
	ub_ports_unset(uent);

	message_remove_device(uent);
	ub_ubc_put(uent->ubc);

	kfree(uent->driver_override);
	uent->token_value = 0;
	kfree(uent);
	pr_info("uent release\n");
}

void ub_stop_ent(struct ub_entity *uent)
{
	if (!uent)
		return;

	if (!ub_entity_test_priv_flag(uent, UB_ENTITY_START))
		return;
	ub_entity_assign_priv_flag(uent, UB_ENTITY_START, false);

	device_release_driver(&uent->dev);
	uent->match_driver = false;
}
EXPORT_SYMBOL_GPL(ub_stop_ent);

void ub_remove_ent(struct ub_entity *uent)
{
	if (!uent->dev.kobj.parent)
		return;

	ub_ports_del(uent);

	device_del(&uent->dev);
	down_write(&ub_bus_sem);
	list_del(&uent->node);
	up_write(&ub_bus_sem);

	ub_unconfigure_ent(uent);
	ub_entity_unset_mmio(uent);
	ub_entity_num_free(uent);
	ub_eid_free(uent);
	ub_entity_assign_priv_flag(uent, UB_ENTITY_SETUP, false);

	put_device(&uent->dev);
}

void ub_stop_and_remove_ent(struct ub_entity *uent)
{
	if (!uent)
		return;

	ub_stop_ent(uent);
	ub_remove_ent(uent);
}
EXPORT_SYMBOL_GPL(ub_stop_and_remove_ent);

void ub_stop_entities(void)
{
}

void ub_remove_entities(void)
{
}

// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved.
 * Description: UMMU Framework's implementations.
 */

#include <linux/device.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <ub/ubfi/ubfi.h>
#include <linux/xarray.h>
#include <linux/cleanup.h>
#include <linux/ummu_core.h>

#include "logic_ummu.h"

struct logic_ummu_domain {
	struct ummu_base_domain base_domain;
	struct ummu_base_domain *agent_domain;
};

struct logic_ummu_device {
	struct ummu_core_device core_dev;
	struct ummu_device *agent_device;
	struct list_head dev_list;
	struct mutex dev_mutex;
	u32 ummu_cnt;
};

struct eid_info {
	enum eid_type type;
	eid_t eid;
	guid_t guid;
	struct list_head list;
};

static DEFINE_SPINLOCK(eid_list_lock);
static LIST_HEAD(cached_eid_list);
static DEFINE_XARRAY(logic_ummu_ops_info);
static u32 global_ummu_cnt;
static struct logic_ummu_device logic_ummu;
static struct platform_device *logic_ummu_dev;
const struct fwnode_operations logic_ummu_static_fwnode_ops;
static struct fwnode_handle *logic_ummu_fwnode;

static inline struct logic_ummu_domain *
base_to_logic_domain(struct ummu_base_domain *dom)
{
	return container_of(dom, struct logic_ummu_domain, base_domain);
}

static inline struct logic_ummu_domain *
iommu_to_logic_domain(struct iommu_domain *dom)
{
	struct ummu_base_domain *base_domain;

	base_domain = container_of(dom, struct ummu_base_domain, domain);
	return base_to_logic_domain(base_domain);
}

struct iommu_domain *iommu_to_agent_domain(struct iommu_domain *dom)
{
	struct ummu_base_domain *base_domain, *agent_domain;

	base_domain = container_of(dom, struct ummu_base_domain, domain);
	agent_domain = base_to_logic_domain(base_domain)->agent_domain;
	return &agent_domain->domain;
}

static inline const struct iommu_ops *get_agent_iommu_ops(void)
{
	if (!logic_ummu.agent_device)
		return NULL;

	return logic_ummu.agent_device->core_dev.iommu.ops;
}

static inline const struct ummu_core_ops *get_agent_core_ops(void)
{
	if (!logic_ummu.agent_device)
		return NULL;

	return logic_ummu.agent_device->core_dev.ops;
}

static inline const struct ummu_device_helper *get_agent_helper(void)
{
	if (!logic_ummu.agent_device)
		return NULL;

	return logic_ummu.agent_device->helper_ops;
}

static struct iommu_ops logic_iommu_ops = {
	.pgsize_bitmap = SZ_4K,
	.owner = THIS_MODULE,
};

static u32 get_ummu_count(void)
{
	return ubrt_fwnode_get_count(UBRT_UMMU);
}

static void remove_all_eid(void)
{
	const struct ummu_core_ops *core_ops = get_agent_core_ops();
	struct eid_info *info, *next;
	struct ummu_device *ummu;

	if (!core_ops || !core_ops->del_eid) {
		pr_err("invalid core_ops.\n");
		return;
	}
	guard(spinlock)(&eid_list_lock);

	list_for_each_entry_safe(info, next, &cached_eid_list, list) {
		list_for_each_entry(ummu, &logic_ummu.dev_list, list)
			core_ops->del_eid(&ummu->core_dev, &info->guid, info->eid, info->type);

		list_del(&info->list);
		kfree(info);
	}
}

static struct ummu_core_ops logic_ummu_core_ops = {};

static int init_ummu_device(struct ummu_device *ummu,
			    const struct iommu_ops *iommu_ops,
			    const struct ummu_core_ops *core_ops)
{
	struct ummu_core_init_args args = { 0 };

	args.iommu_ops = iommu_ops;
	args.core_ops = core_ops;
	args.hwdev = ummu->dev;
	args.tid_args.tid_ops = NULL;

	return ummu_core_device_init(&ummu->core_dev, &args);
}

static int logic_ummu_core_device_init(void)
{
	struct ummu_core_init_args args = { 0 };
	struct ummu_device *entry;
	int ret;

	args.iommu_ops = &logic_iommu_ops;
	args.core_ops = &logic_ummu_core_ops;
	args.hwdev = &logic_ummu_dev->dev;
	args.tid_args.tid_ops = ummu_core_tid_ops[PASID_OPS];
	args.tid_args.max_tid = logic_ummu.core_dev.iommu.max_pasids;
	args.tid_args.min_tid = logic_ummu.core_dev.iommu.min_pasids;
	ret = ummu_core_device_init(&logic_ummu.core_dev, &args);
	if (ret) {
		pr_err("init ummu core device failed.\n");
		return ret;
	}
	/* sync tid manager to all device */
	list_for_each_entry(entry, &logic_ummu.dev_list, list)
		entry->core_dev.tid_manager = logic_ummu.core_dev.tid_manager;

	return 0;
}

static void logic_ummu_core_device_deinit(void)
{
	struct ummu_device *entry;

	list_for_each_entry(entry, &logic_ummu.dev_list, list)
		entry->core_dev.tid_manager = NULL;

	ummu_core_device_deinit(&logic_ummu.core_dev);
}

static int logic_ummu_device_add_agent(struct ummu_device *ummu)
{
	struct iommu_domain_ops *domain_ops;
	const struct iommu_ops *drv_ops;

	domain_ops = kzalloc(sizeof(*domain_ops), GFP_KERNEL);
	if (!domain_ops)
		return -ENOMEM;

	drv_ops = ummu->core_dev.iommu.ops;
	logic_ummu.agent_device = ummu;
	logic_ummu.core_dev.iommu.min_pasids = ummu->core_dev.iommu.min_pasids;
	logic_ummu.core_dev.iommu.max_pasids = ummu->core_dev.iommu.max_pasids;
	logic_iommu_ops.pgsize_bitmap = ummu->core_dev.iommu.ops->pgsize_bitmap;
	return 0;
}

static void logic_ummu_device_del_agent(void)
{
	logic_ummu.agent_device = NULL;
	logic_ummu.core_dev.iommu.min_pasids = UMMU_NO_TID;
	logic_ummu.core_dev.iommu.max_pasids = UMMU_NO_TID;
	kfree(logic_iommu_ops.default_domain_ops);
	logic_iommu_ops.default_domain_ops = NULL;
}

static int update_logic_ummu(struct ummu_device *ummu)
{
	int ret;

	logic_ummu.ummu_cnt++;
	list_add_tail(&ummu->list, &logic_ummu.dev_list);
	if (!logic_ummu.agent_device) {
		ret = logic_ummu_device_add_agent(ummu);
		if (ret) {
			pr_err("add agent failed.\n");
			goto out_del_list;
		}
	}
	if (logic_ummu.ummu_cnt == global_ummu_cnt) {
		ret = logic_ummu_core_device_init();
		if (ret) {
			pr_err("logic ummu core device init failed, ret = %d.\n", ret);
			goto out_del_agent;
		}
		ret = ummu_core_device_register(&logic_ummu.core_dev,
						REGISTER_TYPE_GLOBAL);
		if (ret) {
			pr_err("register to ummu core failed, ret = %d.\n", ret);
			goto out_deinit_logic_ummu;
		}
	}
	return 0;

out_deinit_logic_ummu:
	logic_ummu_core_device_deinit();
out_del_agent:
	if (ummu == logic_ummu.agent_device)
		logic_ummu_device_del_agent();
out_del_list:
	list_del(&ummu->list);
	logic_ummu.ummu_cnt--;
	return ret;
}

int logic_add_ummu_device(struct ummu_device *ummu,
			  const struct iommu_ops *iommu_ops,
			  const struct ummu_core_ops *core_ops)
{
	int ret;

	guard(mutex)(&logic_ummu.dev_mutex);
	if (logic_ummu.ummu_cnt >= global_ummu_cnt) {
		pr_err("unexpected ummu was added.\n");
		return -EINVAL;
	}
	ret = init_ummu_device(ummu, iommu_ops, core_ops);
	if (ret) {
		pr_err("setup ummu device failed, ret = %d.\n", ret);
		return ret;
	}
	ret = update_logic_ummu(ummu);
	if (ret) {
		pr_err("update logic ummu failed.\n");
		ummu_core_device_deinit(&ummu->core_dev);
		return ret;
	}

	return 0;
}

void logic_remove_ummu_device(struct ummu_device *ummu)
{
	guard(mutex)(&logic_ummu.dev_mutex);
	if (!logic_ummu.ummu_cnt) {
		pr_err("unexpected number of UMMUs\n");
		return;
	}

	if (logic_ummu.ummu_cnt == global_ummu_cnt) {
		ummu_core_device_unregister(&logic_ummu.core_dev);
		remove_all_eid();
		logic_ummu_core_device_deinit();
	}

	if (ummu == logic_ummu.agent_device)
		logic_ummu_device_del_agent();

	ummu_core_device_deinit(&ummu->core_dev);
	list_del(&ummu->list);
	logic_ummu.ummu_cnt--;

	dev_info(ummu->dev, "logic ummu remove ummu instance successful!\n");
}

static inline struct fwnode_handle *logic_ummu_alloc_fwnode_static(void)
{
	struct fwnode_handle *handle;

	handle = kzalloc(sizeof(*handle), GFP_KERNEL);
	if (!handle)
		return NULL;

	fwnode_init(handle, &logic_ummu_static_fwnode_ops);

	return handle;
}

int logic_ummu_device_init(void)
{
	int ret;

	logic_ummu_dev = platform_device_alloc("logic-ummu", -1);
	if (!logic_ummu_dev) {
		pr_err("alloc logic ummu device failed.\n");
		return -ENOMEM;
	}

	logic_ummu_fwnode = logic_ummu_alloc_fwnode_static();
	if (!logic_ummu_fwnode) {
		ret = -ENOMEM;
		pr_err("logic_ummu_alloc_fwnode_static failed.\n");
		goto out_pdev_put;
	}
	logic_ummu_dev->dev.fwnode = logic_ummu_fwnode;
	logic_ummu_dev->dev.fwnode->dev = &logic_ummu_dev->dev;

	ret = platform_device_add(logic_ummu_dev);
	if (ret) {
		pr_err("add logic ummu device failed\n");
		goto out_free_fwnode;
	}
	ret = iommu_device_sysfs_add(&logic_ummu.core_dev.iommu, NULL, NULL,
				     "%s", "logic_ummu");
	if (ret) {
		pr_err("register logic ummu to sysfs failed.\n");
		goto out_pdev_del;
	}
	logic_ummu.ummu_cnt = 0;
	logic_ummu.core_dev.iommu.max_pasids = UMMU_NO_TID;
	INIT_LIST_HEAD(&logic_ummu.dev_list);
	mutex_init(&logic_ummu.dev_mutex);
	global_ummu_cnt = get_ummu_count();

	return 0;
out_pdev_del:
	platform_device_del(logic_ummu_dev);
out_free_fwnode:
	kfree(logic_ummu_fwnode);
	logic_ummu_fwnode = NULL;
out_pdev_put:
	platform_device_put(logic_ummu_dev);
	return ret;
}

void logic_ummu_device_exit(void)
{
	struct ummu_device *ummu, *next;
	unsigned long index;
	uintptr_t *ops;

	xa_for_each(&logic_ummu_ops_info, index, ops)
		kfree(ops);

	if (logic_ummu.ummu_cnt != 0 || !list_empty(&logic_ummu.dev_list)) {
		list_for_each_entry_safe(ummu, next, &logic_ummu.dev_list, list)
			logic_remove_ummu_device(ummu);

		pr_warn("unexpected ummu instances during exit.\n");
	}
	iommu_device_sysfs_remove(&logic_ummu.core_dev.iommu);
	platform_device_unregister(logic_ummu_dev);
	kfree(logic_ummu_fwnode);
	logic_ummu_fwnode = NULL;
}

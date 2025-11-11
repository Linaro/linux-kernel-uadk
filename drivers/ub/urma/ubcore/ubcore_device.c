// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2021-2025. All rights reserved.
 *
 * Description: ubcore device add and remove ops file
 * Author: Qian Guoxin
 * Create: 2021-08-03
 * Note:
 * History: 2021-08-03: create file
 */

#include <linux/netdevice.h>
#include <linux/version.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include "ub/urma/ubcore_uapi.h"
#include <net/netns/generic.h>

#include "ubcore_log.h"
#include "ubcore_device.h"
#include "ubcore_tp_table.h"
#include "ub/urma/ubcore_jetty.h"
#include "ubcore_workqueue.h"
#include "ubcore_main.h"
#include "ubcore_cdev_file.h"

#define UBCORE_MAX_MUE_NUM 16
#define UBCORE_DEVICE_NAME "ubcore"

struct ubcore_ctx {
	dev_t ubcore_devno;
	struct cdev ubcore_cdev;
	struct device *ubcore_dev;
};

static LIST_HEAD(g_client_list);
static LIST_HEAD(g_device_list);

/*
 * g_device_rwsem and g_lists_rwsem protect both g_device_list and g_client_list.
 * g_device_rwsem protects writer access by device and client
 * g_lists_rwsem protects reader access to these lists.
 * Iterators of these lists must lock it for read, while updates
 * to the lists must be done with a write lock.
 */
static DECLARE_RWSEM(g_device_rwsem);

/*
 * g_clients_rwsem protect g_client_list.
 */
static DECLARE_RWSEM(g_clients_rwsem);

static unsigned int g_ubcore_net_id;
static LIST_HEAD(g_ubcore_net_list);
static DEFINE_SPINLOCK(g_ubcore_net_lock);
static DECLARE_RWSEM(g_ubcore_net_rwsem);

static bool g_shared_ns = true;

static int ubcore_global_open(struct inode *i_node, struct file *filp)
{
	ubcore_log_info("open ubcore global file succeed.\n");
	return 0;
}

static long ubcore_global_ioctl(struct file *filp, unsigned int cmd,
				unsigned long arg)
{
	ubcore_log_err("bad ioctl command.\n");
	return -ENOIOCTLCMD;
}

static int ubcore_global_close(struct inode *i_node, struct file *filp)
{
	ubcore_log_info("closing ubcore global device.\n");
	return 0;
}

static const struct file_operations g_ubcore_global_ops = {
	.owner = THIS_MODULE,
	.open = ubcore_global_open,
	.release = ubcore_global_close,
	.unlocked_ioctl = ubcore_global_ioctl,
	.compat_ioctl = ubcore_global_ioctl,
};

static dev_t g_dynamic_mue_devnum;
static struct ubcore_ctx g_ubcore_ctx = { 0 };

static char *ubcore_devnode(const struct device *dev, umode_t *mode)

{
	if (mode)
		*mode = UBCORE_DEVNODE_MODE;

	return kasprintf(GFP_KERNEL, "ubcore/%s", dev_name(dev));
}

static struct class g_ubcore_class = { .name = "ubcore",
				       .devnode = ubcore_devnode,
				       .ns_type = &net_ns_type_operations,
				       .namespace = NULL };
struct ubcore_net {
	possible_net_t net;
	struct list_head node;
};

struct ubcore_upi_entry {
	struct ubcore_device *dev;
	uint32_t upi;
	struct list_head node;
};

struct ubcore_event_work {
	struct work_struct work;
	struct ubcore_event event;
};

int ubcore_class_register(void)
{
	int ret;

	// Allocate device numbers for MUE
	ret = alloc_chrdev_region(&g_dynamic_mue_devnum, 0, UBCORE_MAX_MUE_NUM,
				  UBCORE_DEVICE_NAME);
	if (ret != 0) {
		ubcore_log_err(
			"couldn't register dynamic device number for mue.\n");
		return ret;
	}

	ret = class_register(&g_ubcore_class);
	if (ret) {
		unregister_chrdev_region(g_dynamic_mue_devnum,
					 UBCORE_MAX_MUE_NUM);
		ubcore_log_err("couldn't create ubcore class\n");
	}
	return ret;
}

void ubcore_class_unregister(void)
{
	class_unregister(&g_ubcore_class);
	unregister_chrdev_region(g_dynamic_mue_devnum, UBCORE_MAX_MUE_NUM);
}

int ubcore_cdev_register(void)
{
	int ret;

	// If sysfs is created, return Success
	// Need to add mutex
	if (!IS_ERR_OR_NULL(g_ubcore_ctx.ubcore_dev))
		return 0;

	ret = alloc_chrdev_region(&g_ubcore_ctx.ubcore_devno, 0, 1,
				  UBCORE_DEVICE_NAME);
	if (ret != 0) {
		ubcore_log_err("alloc chrdev region failed, ret:%d.\n", ret);
		return ret;
	}

	cdev_init(&g_ubcore_ctx.ubcore_cdev, &g_ubcore_global_ops);
	g_ubcore_ctx.ubcore_cdev.owner = THIS_MODULE;

	ret = cdev_add(&g_ubcore_ctx.ubcore_cdev, g_ubcore_ctx.ubcore_devno, 1);
	if (ret != 0) {
		ubcore_log_err("chrdev add failed, ret:%d.\n", ret);
		goto unreg_cdev_region;
	}

	/* /dev/ubcore */
	g_ubcore_ctx.ubcore_dev = device_create(&g_ubcore_class, NULL,
						g_ubcore_ctx.ubcore_devno, NULL,
						UBCORE_DEVICE_NAME);
	if (IS_ERR(g_ubcore_ctx.ubcore_dev)) {
		ret = (int)PTR_ERR(g_ubcore_ctx.ubcore_dev);
		ubcore_log_err("couldn't create device %s, ret:%d.\n",
			       UBCORE_DEVICE_NAME, ret);
		g_ubcore_ctx.ubcore_dev = NULL;
		goto del_cdev;
	}
	ubcore_log_info("ubcore device created success.\n");
	return 0;

del_cdev:
	cdev_del(&g_ubcore_ctx.ubcore_cdev);
unreg_cdev_region:
	unregister_chrdev_region(g_ubcore_ctx.ubcore_devno, 1);
	return ret;
}

int ubcore_cdev_unregister(void)
{
	// If sysfs is not created, return Success
	// Need to add mutex
	if (IS_ERR_OR_NULL(g_ubcore_ctx.ubcore_dev))
		return 0;

	device_destroy(&g_ubcore_class, g_ubcore_ctx.ubcore_cdev.dev);
	cdev_del(&g_ubcore_ctx.ubcore_cdev);
	unregister_chrdev_region(g_ubcore_ctx.ubcore_devno, 1);
	ubcore_log_info("ubcore sysfs device destroyed success.\n");
	return 0;
}

void ubcore_get_device(struct ubcore_device *dev)
{
	if (IS_ERR_OR_NULL(dev)) {
		ubcore_log_err("Invalid parameter\n");
		return;
	}

	atomic_inc(&dev->use_cnt);
}

void ubcore_put_device(struct ubcore_device *dev)
{
	if (IS_ERR_OR_NULL(dev)) {
		ubcore_log_err("Invalid parameter\n");
		return;
	}

	if (atomic_dec_and_test(&dev->use_cnt))
		complete(&dev->comp);
}

struct ubcore_device *ubcore_find_device(union ubcore_eid *eid,
					 enum ubcore_transport_type type)
{
	struct ubcore_device *dev, *target = NULL;
	uint32_t idx;

	down_read(&g_device_rwsem);
	list_for_each_entry(dev, &g_device_list, list_node) {
		if (IS_ERR_OR_NULL(dev->eid_table.eid_entries))
			continue;
		for (idx = 0; idx < dev->attr.dev_cap.max_eid_cnt; idx++) {
			if (memcmp(&dev->eid_table.eid_entries[idx].eid, eid,
				   sizeof(union ubcore_eid)) == 0 &&
			    dev->transport_type == type) {
				target = dev;
				ubcore_get_device(target);
				break;
			}
		}
		if (target != NULL)
			break;
	}
	up_read(&g_device_rwsem);
	return target;
}

struct ubcore_device *ubcore_find_device_with_name(const char *dev_name)
{
	struct ubcore_device *dev, *target = NULL;

	down_read(&g_device_rwsem);
	list_for_each_entry(dev, &g_device_list, list_node) {
		if (strcmp(dev->dev_name, dev_name) == 0) {
			target = dev;
			ubcore_get_device(target);
			break;
		}
	}
	up_read(&g_device_rwsem);
	return target;
}

static int ubcore_create_eidtable(struct ubcore_device *dev)
{
	struct ubcore_eid_entry *entry_list;

	if (dev->attr.dev_cap.max_eid_cnt > UBCORE_MAX_EID_CNT ||
	    dev->attr.dev_cap.max_eid_cnt == 0) {
		ubcore_log_err("dev max_eid_cnt invalid:%u\n",
			       dev->attr.dev_cap.max_eid_cnt);
		return -EINVAL;
	}

	entry_list = kcalloc(1,
			     dev->attr.dev_cap.max_eid_cnt *
				     sizeof(struct ubcore_eid_entry),
			     GFP_KERNEL);
	if (entry_list == NULL)
		return -ENOMEM;

	dev->eid_table.eid_entries = entry_list;
	spin_lock_init(&dev->eid_table.lock);
	dev->eid_table.eid_cnt = dev->attr.dev_cap.max_eid_cnt;
	dev->dynamic_eid = 1;
	return 0;
}

static void ubcore_destroy_eidtable(struct ubcore_device *dev)
{
	struct ubcore_eid_entry *e = NULL;

	spin_lock(&dev->eid_table.lock);
	e = dev->eid_table.eid_entries;
	dev->eid_table.eid_entries = NULL;
	spin_unlock(&dev->eid_table.lock);
	if (e != NULL)
		kfree(e);
}

static void ubcore_free_driver_obj(void *obj)
{
	// obj alloced by driver, should not free by ubcore
	ubcore_log_err("obj was not free correctly!");
}

static struct ubcore_ht_param g_ht_params[] = {
	[UBCORE_HT_JFS] = { UBCORE_HASH_TABLE_SIZE,
			    offsetof(struct ubcore_jfs, hnode),
			    offsetof(struct ubcore_jfs, jfs_id) +
				    offsetof(struct ubcore_jetty_id, id),
			    sizeof(uint32_t), NULL, ubcore_free_driver_obj,
			    ubcore_jfs_get },

	[UBCORE_HT_JFR] = { UBCORE_HASH_TABLE_SIZE,
			    offsetof(struct ubcore_jfr, hnode),
			    offsetof(struct ubcore_jfr, jfr_id) +
				    offsetof(struct ubcore_jetty_id, id),
			    sizeof(uint32_t), NULL, ubcore_free_driver_obj,
			    ubcore_jfr_get },
	[UBCORE_HT_JFC] = { UBCORE_HASH_TABLE_SIZE,
			    offsetof(struct ubcore_jfc, hnode),
			    offsetof(struct ubcore_jfc, id), sizeof(uint32_t),
			    NULL, ubcore_free_driver_obj, NULL },

	[UBCORE_HT_JETTY] = { UBCORE_HASH_TABLE_SIZE,
			      offsetof(struct ubcore_jetty, hnode),
			      offsetof(struct ubcore_jetty, jetty_id) +
				      offsetof(struct ubcore_jetty_id, id),
			      sizeof(uint32_t), NULL, ubcore_free_driver_obj,
			      ubcore_jetty_get },
};

static inline void ubcore_set_vtp_hash_table_size(uint32_t vtp_size)
{
	if (vtp_size == 0 || vtp_size > UBCORE_HASH_TABLE_SIZE)
		return;
	g_ht_params[UBCORE_HT_RM_VTP].size = vtp_size;
	g_ht_params[UBCORE_HT_RC_VTP].size = vtp_size;
	g_ht_params[UBCORE_HT_UM_VTP].size = vtp_size;
}

static inline void ubcore_set_vtpn_hash_table_size(uint32_t vtpn_size)
{
	if (vtpn_size == 0 || vtpn_size > UBCORE_HASH_TABLE_SIZE)
		return;
	g_ht_params[UBCORE_HT_RM_VTPN].size = vtpn_size;
	g_ht_params[UBCORE_HT_RC_VTPN].size = vtpn_size;
	g_ht_params[UBCORE_HT_UM_VTPN].size = vtpn_size;
	g_ht_params[UBCORE_HT_CP_VTPN].size = vtpn_size;
	g_ht_params[UBCORE_HT_VTPN].size = vtpn_size;
}

static void ubcore_update_hash_tables_size(const struct ubcore_device_cap *cap)
{
	if (cap->max_jfs != 0 && cap->max_jfs < g_ht_params[UBCORE_HT_JFS].size)
		g_ht_params[UBCORE_HT_JFS].size = cap->max_jfs;
	if (cap->max_jfr != 0 && cap->max_jfr < g_ht_params[UBCORE_HT_JFR].size)
		g_ht_params[UBCORE_HT_JFR].size = cap->max_jfr;
	if (cap->max_jfc != 0 && cap->max_jfc < g_ht_params[UBCORE_HT_JFC].size)
		g_ht_params[UBCORE_HT_JFC].size = cap->max_jfc;
	if (cap->max_jetty != 0 &&
	    cap->max_jetty < g_ht_params[UBCORE_HT_JETTY].size)
		g_ht_params[UBCORE_HT_JETTY].size = cap->max_jetty;
	if (cap->max_tp_cnt != 0 &&
	    cap->max_tp_cnt < g_ht_params[UBCORE_HT_TP].size)
		g_ht_params[UBCORE_HT_TP].size = cap->max_tp_cnt;
	if (cap->max_tpg_cnt != 0 &&
	    cap->max_tpg_cnt < g_ht_params[UBCORE_HT_TPG].size)
		g_ht_params[UBCORE_HT_TPG].size = cap->max_tpg_cnt;
	if (cap->max_vtp_cnt_per_ue < UBCORE_HASH_TABLE_SIZE &&
	    cap->max_ue_cnt < UBCORE_HASH_TABLE_SIZE)
		ubcore_set_vtp_hash_table_size(
			(cap->max_vtp_cnt_per_ue * cap->max_ue_cnt));
	ubcore_set_vtpn_hash_table_size(cap->max_vtp_cnt_per_ue);

	if (cap->max_utp_cnt != 0 &&
	    cap->max_utp_cnt < g_ht_params[UBCORE_HT_UTP].size)
		g_ht_params[UBCORE_HT_UTP].size = cap->max_utp_cnt;
	/* ctp size use max_tp_cnt */
	if (cap->max_tp_cnt != 0 &&
	    cap->max_tp_cnt < g_ht_params[UBCORE_HT_CTP].size)
		g_ht_params[UBCORE_HT_CTP].size = cap->max_tp_cnt;
}

static int ubcore_alloc_hash_tables(struct ubcore_device *dev)
{
	uint32_t i, j;
	int ret;

	ubcore_update_hash_tables_size(&dev->attr.dev_cap);
	for (i = 0; i < ARRAY_SIZE(g_ht_params); i++) {
		ret = ubcore_hash_table_alloc(&dev->ht[i], &g_ht_params[i]);
		if (ret != 0) {
			ubcore_log_err("alloc hash tables failed.\n");
			goto free_tables;
		}
	}

	return 0;

free_tables:
	for (j = 0; j < i; j++)
		ubcore_hash_table_free(&dev->ht[j]);
	return -1;
}

static void ubcore_free_hash_tables(struct ubcore_device *dev)
{
	uint32_t i;

	for (i = 0; i < ARRAY_SIZE(g_ht_params); i++)
		ubcore_hash_table_free(&dev->ht[i]);
}

static void ubcore_destroy_vtp_in_unreg_dev(void *arg)
{
	struct ubcore_vtp *vtp = (struct ubcore_vtp *)arg;

	if (vtp->cfg.vtpn != UINT_MAX && vtp->ub_dev->ops->destroy_vtp != NULL)
		(void)vtp->ub_dev->ops->destroy_vtp(vtp);
	else
		kfree(vtp);
}

static void ubcore_destroy_tp_in_unreg_dev(void *arg)
{
	struct ubcore_tp *tp = (struct ubcore_tp *)arg;

	if (tp->ub_dev->ops->destroy_tp != NULL)
		(void)tp->ub_dev->ops->destroy_tp(tp);
}

static void ubcore_destroy_utp_in_unreg_dev(void *arg)
{
	struct ubcore_utp *utp = (struct ubcore_utp *)arg;

	if (utp->ub_dev->ops->destroy_utp != NULL)
		(void)utp->ub_dev->ops->destroy_utp(utp);
}

static void ubcore_destroy_tpg_in_unreg_dev(void *arg)
{
	struct ubcore_tpg *tpg = (struct ubcore_tpg *)arg;

	if (tpg->ub_dev->ops->destroy_tpg != NULL)
		(void)tpg->ub_dev->ops->destroy_tpg(tpg);
}

static void ubcore_free_driver_res(struct ubcore_device *dev)
{
	if (!dev->attr.tp_maintainer)
		return;

	ubcore_hash_table_free_with_cb(&dev->ht[UBCORE_HT_RM_VTP],
				       ubcore_destroy_vtp_in_unreg_dev);
	ubcore_hash_table_free_with_cb(&dev->ht[UBCORE_HT_RC_VTP],
				       ubcore_destroy_vtp_in_unreg_dev);
	ubcore_hash_table_free_with_cb(&dev->ht[UBCORE_HT_UM_VTP],
				       ubcore_destroy_vtp_in_unreg_dev);
	ubcore_hash_table_free_with_cb(&dev->ht[UBCORE_HT_TP],
				       ubcore_destroy_tp_in_unreg_dev);
	ubcore_hash_table_free_with_cb(&dev->ht[UBCORE_HT_UTP],
				       ubcore_destroy_utp_in_unreg_dev);

	ubcore_hash_table_free_with_cb(&dev->ht[UBCORE_HT_TPG],
				       ubcore_destroy_tpg_in_unreg_dev);
}

static int init_ubcore_device(struct ubcore_device *dev)
{
	if (dev->ops->query_device_attr != NULL &&
	    dev->ops->query_device_attr(dev, &dev->attr) != 0) {
		ubcore_log_err("Failed to query device attributes");
		return -1;
	}

	INIT_LIST_HEAD(&dev->list_node);
	init_rwsem(&dev->client_ctx_rwsem);
	INIT_LIST_HEAD(&dev->client_ctx_list);
	INIT_LIST_HEAD(&dev->port_list);
	init_rwsem(&dev->event_handler_rwsem);
	INIT_LIST_HEAD(&dev->event_handler_list);

	init_completion(&dev->comp);
	atomic_set(&dev->use_cnt, 1);

	if (ubcore_create_eidtable(dev) != 0) {
		ubcore_log_err("create eidtable failed.\n");
		return -1;
	}

	if (ubcore_alloc_hash_tables(dev) != 0) {
		ubcore_log_err("alloc hash tables failed.\n");
		goto destroy_eidtable;
	}

	mutex_init(&dev->ldev_mutex);
	INIT_LIST_HEAD(&dev->ldev_list);
	return 0;

destroy_eidtable:
	ubcore_destroy_eidtable(dev);
	return -1;
}

static void ubcore_device_release(struct device *device)
{
}

static int ubcore_create_main_device(struct ubcore_device *dev)
{
	struct ubcore_logic_device *ldev = &dev->ldev;
	struct net *net = &init_net;
	int ret;

	/* create /sys/class/ubcore/<dev->dev_name> */
	write_pnet(&ldev->net, net);
	ldev->ub_dev = dev;
	ldev->dev = &dev->dev;

	device_initialize(&dev->dev);
	dev->dev.class = &g_ubcore_class;
	dev->dev.release = ubcore_device_release;
	/* dev_set_name will alloc mem use put_device to free */
	(void)dev_set_name(&dev->dev, "%s", dev->dev_name);
	dev_set_drvdata(&dev->dev, ldev);

	ret = device_add(&dev->dev);
	if (ret) {
		put_device(&dev->dev); // to free res used by kobj
		return ret;
	}

	if (ubcore_fill_logic_device_attr(ldev, dev) != 0) {
		device_del(&dev->dev);
		put_device(&dev->dev);
		ldev->dev = NULL;
		ubcore_log_err("failed to fill attributes, device:%s.\n",
			       dev->dev_name);
		return -EPERM;
	}

	return 0;
}

static void uninit_ubcore_device(struct ubcore_device *dev)
{
	mutex_destroy(&dev->ldev_mutex);
	ubcore_free_driver_res(dev);
	ubcore_free_hash_tables(dev);
	ubcore_destroy_eidtable(dev);
}

int ubcore_config_rsvd_jetty(struct ubcore_device *dev, uint32_t min_jetty_id,
			     uint32_t max_jetty_id)
{
	struct ubcore_device_cfg cfg = { 0 };
	int ret = 0;

	if (dev == NULL || dev->ops == NULL ||
	    dev->ops->config_device == NULL ||
	    dev->ops->query_device_attr == NULL ||
	    dev->transport_type != UBCORE_TRANSPORT_UB) {
		return -EINVAL;
	}

	cfg.ue_idx = dev->attr.ue_idx;
	cfg.mask.bs.reserved_jetty_id_min = 1;
	cfg.mask.bs.reserved_jetty_id_max = 1;
	cfg.reserved_jetty_id_min = min_jetty_id;
	cfg.reserved_jetty_id_max = max_jetty_id;

	ret = dev->ops->config_device(dev, &cfg);
	if (ret) {
		ubcore_log_info("dev:%s, not support reserved jetty\n",
				dev->dev_name);
		dev->attr.reserved_jetty_id_max = U32_MAX;
		dev->attr.reserved_jetty_id_min = U32_MAX;
	} else {
		dev->ops->query_device_attr(dev, &dev->attr);
	}

	return ret;
}

static int ubcore_config_device_default(struct ubcore_device *dev)
{
	struct ubcore_device_cfg cfg = { 0 };

	if (dev == NULL || dev->ops == NULL ||
	    dev->ops->config_device == NULL) {
		ubcore_log_err("Invalid parameter.\n");
		return -EINVAL;
	}

	cfg.ue_idx = dev->attr.ue_idx;

	cfg.mask.bs.rc_cnt = 1;
	cfg.mask.bs.rc_depth = 1;
	cfg.rc_cfg.rc_cnt = dev->attr.dev_cap.max_rc;
	cfg.rc_cfg.depth = dev->attr.dev_cap.max_rc_depth;

	(void)ubcore_config_rsvd_jetty(dev, UBCORE_RESERVED_JETTY_ID_MIN,
				       UBCORE_RESERVED_JETTY_ID_MAX);
	/* slice and mask.slice are set to 0 by default */

	/* If suspend_period and cnt cannot be read, do not need to configure it */
	return dev->ops->config_device(dev, &cfg);
}

static int ubcore_config_device_in_register(struct ubcore_device *dev)
{
	return ubcore_config_device_default(dev);
}

void ubcore_set_client_ctx_data(struct ubcore_device *dev,
				struct ubcore_client *client, void *data)
{
	struct ubcore_client_ctx *ctx;

	if (dev == NULL || client == NULL || client->client_name == NULL ||
	    strnlen(dev->dev_name, UBCORE_MAX_DEV_NAME) >=
		    UBCORE_MAX_DEV_NAME ||
	    strnlen(client->client_name, UBCORE_MAX_DEV_NAME) >=
		    UBCORE_MAX_DEV_NAME) {
		ubcore_log_err("dev or client is null");
		return;
	}

	down_read(&dev->client_ctx_rwsem);
	list_for_each_entry(ctx, &dev->client_ctx_list, list_node) {
		if (ctx->client == client) {
			ctx->data = data;
			goto out;
		}
	}
	ubcore_log_err(
		"no client ctx found, device_name: %s, client_name: %s.\n",
		dev->dev_name, client->client_name);

out:
	up_read(&dev->client_ctx_rwsem);
}
EXPORT_SYMBOL(ubcore_set_client_ctx_data);

static struct ubcore_client_ctx *
ubcore_lookup_client_context(struct ubcore_device *dev,
			     struct ubcore_client *client)
{
	struct ubcore_client_ctx *found_ctx = NULL;
	struct ubcore_client_ctx *ctx, *tmp;

	if (dev == NULL || client == NULL) {
		ubcore_log_err("dev is null");
		return NULL;
	}

	down_read(&dev->client_ctx_rwsem);
	list_for_each_entry_safe(ctx, tmp, &dev->client_ctx_list, list_node) {
		if (ctx->client == client) {
			found_ctx = ctx;
			break;
		}
	}
	up_read(&dev->client_ctx_rwsem);
	return found_ctx;
}

void *ubcore_get_client_ctx_data(struct ubcore_device *dev,
				 struct ubcore_client *client)
{
	struct ubcore_client_ctx *found_ctx = NULL;

	if (dev == NULL || client == NULL || client->client_name == NULL ||
	    strnlen(dev->dev_name, UBCORE_MAX_DEV_NAME) >=
		    UBCORE_MAX_DEV_NAME ||
	    strnlen(client->client_name, UBCORE_MAX_DEV_NAME) >=
		    UBCORE_MAX_DEV_NAME) {
		ubcore_log_err("dev or client is null");
		return NULL;
	}

	found_ctx = ubcore_lookup_client_context(dev, client);
	if (found_ctx == NULL) {
		ubcore_log_warn(
			"no client ctx found, dev_name: %s, client_name: %s.\n",
			dev->dev_name, client->client_name);
		return NULL;
	} else {
		return found_ctx->data;
	}
}
EXPORT_SYMBOL(ubcore_get_client_ctx_data);

static int create_client_ctx(struct ubcore_device *dev,
			     struct ubcore_client *client)
{
	struct ubcore_client_ctx *ctx;

	ctx = kmalloc(sizeof(struct ubcore_client_ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->data = NULL;
	ctx->client = client;

	down_write(&dev->client_ctx_rwsem);
	list_add(&ctx->list_node, &dev->client_ctx_list);
	downgrade_write(&dev->client_ctx_rwsem);
	if (client->add && client->add(dev) != 0) {
		list_del(&ctx->list_node);
		kfree(ctx);
		up_read(&dev->client_ctx_rwsem);
		return -EPERM;
	}
	up_read(&dev->client_ctx_rwsem);
	return 0;
}

static void destroy_client_ctx(struct ubcore_device *dev,
			       struct ubcore_client_ctx *ctx)
{
	if (dev == NULL || ctx == NULL)
		return;

	down_write(&dev->client_ctx_rwsem);
	list_del(&ctx->list_node);
	kfree(ctx);
	up_write(&dev->client_ctx_rwsem);
}

int ubcore_register_client(struct ubcore_client *new_client)
{
	struct ubcore_device *dev;

	if (new_client == NULL || new_client->client_name == NULL ||
	    new_client->add == NULL || new_client->remove == NULL) {
		ubcore_log_err("Invalid parameter.\n");
		return -1;
	}

	if (strnlen(new_client->client_name, UBCORE_MAX_DEV_NAME) >=
	    UBCORE_MAX_DEV_NAME) {
		ubcore_log_err("Invalid parameter, client name.\n");
		return -1;
	}

	down_write(&g_device_rwsem);
	list_for_each_entry(dev, &g_device_list, list_node) {
		if (create_client_ctx(dev, new_client) != 0)
			ubcore_log_warn(
				"ubcore device: %s add client:%s context failed.\n",
				dev->dev_name, new_client->client_name);
	}
	down_write(&g_clients_rwsem);
	list_add_tail(&new_client->list_node, &g_client_list);
	up_write(&g_clients_rwsem);

	up_write(&g_device_rwsem);

	ubcore_log_info("ubcore client: %s register success.\n",
			new_client->client_name);
	return 0;
}
EXPORT_SYMBOL(ubcore_register_client);

void ubcore_unregister_client(struct ubcore_client *rm_client)
{
	struct ubcore_client_ctx *found_ctx = NULL;
	struct ubcore_device *dev;

	if (rm_client == NULL || rm_client->client_name == NULL ||
	    rm_client->add == NULL || rm_client->remove == NULL) {
		ubcore_log_err("Invalid parameter");
		return;
	}
	if (strnlen(rm_client->client_name, UBCORE_MAX_DEV_NAME) >=
	    UBCORE_MAX_DEV_NAME) {
		ubcore_log_err("Invalid parameter, client name.\n");
		return;
	}

	down_write(&g_device_rwsem);

	down_write(&g_clients_rwsem);
	list_del(&rm_client->list_node);
	up_write(&g_clients_rwsem);

	downgrade_write(&g_device_rwsem);
	list_for_each_entry(dev, &g_device_list, list_node) {
		found_ctx = ubcore_lookup_client_context(dev, rm_client);
		if (found_ctx == NULL) {
			ubcore_log_warn(
				"no client ctx found, dev_name: %s, client_name: %s.\n",
				dev->dev_name, rm_client->client_name);
			continue;
		}
		if (rm_client->remove)
			rm_client->remove(dev, found_ctx->data);

		destroy_client_ctx(dev, found_ctx);
		ubcore_log_info(
			"dev remove client, dev_name: %s, client_name: %s.\n",
			dev->dev_name, rm_client->client_name);
	}

	up_read(&g_device_rwsem);
	ubcore_log_info("ubcore client: %s unregister success.\n",
			rm_client->client_name);
}
EXPORT_SYMBOL(ubcore_unregister_client);

static void ubcore_clients_add(struct ubcore_device *dev)
{
	struct ubcore_client *client = NULL;

	down_read(&g_clients_rwsem);
	list_for_each_entry(client, &g_client_list, list_node) {
		if (create_client_ctx(dev, client) != 0)
			ubcore_log_warn(
				"ubcore device: %s add client:%s context failed.\n",
				dev->dev_name, client->client_name);
	}
	up_read(&g_clients_rwsem);
}

static void ubcore_clients_remove(struct ubcore_device *dev)
{
	struct ubcore_client_ctx *ctx, *tmp;

	down_read(&dev->client_ctx_rwsem);
	list_for_each_entry_safe(ctx, tmp, &dev->client_ctx_list, list_node) {
		if (ctx->client && ctx->client->remove)
			ctx->client->remove(dev, ctx->data);
	}
	up_read(&dev->client_ctx_rwsem);

	down_write(&dev->client_ctx_rwsem);
	list_for_each_entry_safe(ctx, tmp, &dev->client_ctx_list, list_node) {
		list_del(&ctx->list_node);
		kfree(ctx);
	}
	up_write(&dev->client_ctx_rwsem);
}

static int ubcore_create_logic_device(struct ubcore_logic_device *ldev,
				      struct ubcore_device *dev,
				      struct net *net)
{
	/* create /sys/class/ubcore/<dev->dev_name> */
	write_pnet(&ldev->net, net);
	ldev->ub_dev = dev;

	ldev->dev = device_create(&g_ubcore_class, dev->dev.parent, MKDEV(0, 0),
				  ldev, "%s", dev->dev_name);
	if (IS_ERR(ldev->dev)) {
		ubcore_log_err("device create failed, device:%s.\n",
			       dev->dev_name);
		return -ENOMEM;
	}

	if (ubcore_fill_logic_device_attr(ldev, dev) != 0) {
		device_unregister(ldev->dev);
		ldev->dev = NULL;
		ubcore_log_err("failed to fill attributes, device:%s.\n",
			       dev->dev_name);
		return -EPERM;
	}

	return 0;
}

static void ubcore_destroy_logic_device(struct ubcore_logic_device *ldev,
					struct ubcore_device *dev)
{
	ubcore_unfill_logic_device_attr(ldev, dev);
	device_unregister(ldev->dev);
	ldev->dev = NULL;
}

static void ubcore_remove_one_logic_device(struct ubcore_device *dev,
					   struct net *net)
{
	struct ubcore_logic_device *ldev, *tmp;

	mutex_lock(&dev->ldev_mutex);
	list_for_each_entry_safe(ldev, tmp, &dev->ldev_list, node) {
		if (net_eq(read_pnet(&ldev->net), net)) {
			ubcore_destroy_logic_device(ldev, dev);
			list_del(&ldev->node);
			kfree(ldev);
			break;
		}
	}
	mutex_unlock(&dev->ldev_mutex);
}

static void ubcore_remove_logic_devices(struct ubcore_device *dev)
{
	struct ubcore_logic_device *ldev, *tmp;

	if (dev->transport_type != UBCORE_TRANSPORT_UB)
		return;

	mutex_lock(&dev->ldev_mutex);
	list_for_each_entry_safe(ldev, tmp, &dev->ldev_list, node) {
		ubcore_destroy_logic_device(ldev, dev);
		list_del(&ldev->node);
		kfree(ldev);
	}
	mutex_unlock(&dev->ldev_mutex);
}

static int ubcore_add_one_logic_device(struct ubcore_device *dev,
				       struct net *net)
{
	struct ubcore_logic_device *ldev;
	int ret;

	mutex_lock(&dev->ldev_mutex);
	list_for_each_entry(ldev, &dev->ldev_list, node) {
		if (net_eq(read_pnet(&ldev->net), net)) {
			mutex_unlock(&dev->ldev_mutex);
			return 0;
		}
	}

	ldev = kzalloc(sizeof(struct ubcore_logic_device), GFP_KERNEL);
	if (ldev == NULL) {
		mutex_unlock(&dev->ldev_mutex);
		return -ENOMEM;
	}

	ret = ubcore_create_logic_device(ldev, dev, net);
	if (ret) {
		kfree(ldev);
		mutex_unlock(&dev->ldev_mutex);
		ubcore_log_err("add device failed %s in net %u", dev->dev_name,
			       net->ns.inum);
		return ret;
	}

	list_add_tail(&ldev->node, &dev->ldev_list);
	mutex_unlock(&dev->ldev_mutex);
	ubcore_log_info_rl("add device %s in net %u", dev->dev_name,
			   net->ns.inum);
	return 0;
}

static int ubcore_copy_logic_devices(struct ubcore_device *dev)
{
	struct ubcore_net *unet;
	int ret = 0;

	if (dev->transport_type != UBCORE_TRANSPORT_UB)
		return 0;

	down_read(&g_ubcore_net_rwsem);
	list_for_each_entry(unet, &g_ubcore_net_list, node) {
		if (net_eq(read_pnet(&unet->net), read_pnet(&dev->ldev.net)))
			continue;
		ret = ubcore_add_one_logic_device(dev, read_pnet(&unet->net));
		if (ret != 0)
			break;
	}
	up_read(&g_ubcore_net_rwsem);

	if (ret)
		ubcore_remove_logic_devices(dev);

	return ret;
}

static void ubcore_destroy_main_device(struct ubcore_device *dev)
{
	struct ubcore_logic_device *ldev = &dev->ldev;

	ubcore_unfill_logic_device_attr(ldev, dev);
	device_del(ldev->dev);
	put_device(ldev->dev);
	ldev->dev = NULL;
}

int ubcore_register_device(struct ubcore_device *dev)
{
	struct ubcore_device *find_dev = NULL;
	int ret;

	if (dev == NULL || dev->ops == NULL ||
	    strnlen(dev->dev_name, UBCORE_MAX_DEV_NAME) == 0 ||
	    strnlen(dev->dev_name, UBCORE_MAX_DEV_NAME) >=
		    UBCORE_MAX_DEV_NAME) {
		ubcore_log_err("Invalid parameter.\n");
		return -EINVAL;
	}

	find_dev = ubcore_find_device_with_name(dev->dev_name);
	if (find_dev != NULL) {
		ubcore_log_err("Duplicate device name %s.\n", dev->dev_name);
		ubcore_put_device(find_dev);
		return -EEXIST;
	}

	if (init_ubcore_device(dev) != 0) {
		ubcore_log_err("failed to init ubcore device.\n");
		return -EINVAL;
	}

	ret = ubcore_create_main_device(dev);
	if (ret) {
		uninit_ubcore_device(dev);
		ubcore_log_err("create main device failed.\n");
		return ret;
	}

	if (ubcore_config_device_in_register(dev) != 0) {
		ubcore_log_err("failed to config ubcore device.\n");
		ret = -EPERM;
		goto destroy_mdev;
	}
	ubcore_cgroup_reg_dev(dev);

	down_write(&g_device_rwsem);
	ubcore_clients_add(dev);
	ret = ubcore_copy_logic_devices(dev);
	if (ret) {
		ubcore_clients_remove(dev);
		up_write(&g_device_rwsem);

		ubcore_log_err("copy logic device failed, device:%s.\n",
			       dev->dev_name);
		goto err;
	}

	list_add_tail(&dev->list_node, &g_device_list);
	up_write(&g_device_rwsem);

	ubcore_log_info_rl("ubcore device: %s register success.\n",
			   dev->dev_name);
	return 0;

err:
	ubcore_cgroup_unreg_dev(dev);
destroy_mdev:
	ubcore_destroy_main_device(dev);
	uninit_ubcore_device(dev);
	return ret;
}
EXPORT_SYMBOL(ubcore_register_device);

void ubcore_unregister_device(struct ubcore_device *dev)
{
	if (dev == NULL || strnlen(dev->dev_name, UBCORE_MAX_DEV_NAME) >=
				   UBCORE_MAX_DEV_NAME) {
		ubcore_log_warn("Invalid input dev is null ptr.\n");
		return;
	}
	down_write(&g_device_rwsem);

	/* Remove device from g_device_list */
	list_del(&dev->list_node);

	/* Destroy uburma device, may be scheduled.
	 * This should not be done within a spin_lock_irqsave
	 */
	up_write(&g_device_rwsem);
	ubcore_clients_remove(dev);

	ubcore_flush_workqueue((int)UBCORE_DISPATCH_EVENT_WQ);
	ubcore_flush_workqueue((int)UBCORE_SIP_NOTIFY_WQ);

	down_read(&g_device_rwsem);
	ubcore_cgroup_unreg_dev(dev);

	ubcore_remove_logic_devices(dev);
	ubcore_destroy_main_device(dev);
	up_read(&g_device_rwsem);

	/* Pair with set use_cnt = 1 when init device */
	ubcore_put_device(dev);
	/* Wait for use cnt == 0 */
	wait_for_completion(&dev->comp);
	uninit_ubcore_device(
		dev); /* Protect eid table access security based on ref cnt */

	ubcore_log_info_rl("ubcore device: %s unregister success.\n",
			   dev->dev_name);
}
EXPORT_SYMBOL(ubcore_unregister_device);

void ubcore_stop_requests(struct ubcore_device *dev)
{
	struct ubcore_client_ctx *ctx, *tmp;

	if (dev == NULL || strnlen(dev->dev_name, UBCORE_MAX_DEV_NAME) >=
				   UBCORE_MAX_DEV_NAME) {
		ubcore_log_err("Invalid parameter");
		return;
	}
	down_read(&dev->client_ctx_rwsem);
	list_for_each_entry_safe(ctx, tmp, &dev->client_ctx_list, list_node) {
		if (ctx->client && ctx->client->stop)
			ctx->client->stop(dev, ctx->data);
	}
	up_read(&dev->client_ctx_rwsem);
	ubcore_log_info("ubcore device: %s stop success.\n", dev->dev_name);
}
EXPORT_SYMBOL(ubcore_stop_requests);

void ubcore_register_event_handler(struct ubcore_device *dev,
				   struct ubcore_event_handler *handler)
{
	if (dev == NULL || handler == NULL) {
		ubcore_log_err("Invalid argument.\n");
		return;
	}

	down_write(&dev->event_handler_rwsem);
	list_add_tail(&handler->node, &dev->event_handler_list);
	up_write(&dev->event_handler_rwsem);
}
EXPORT_SYMBOL(ubcore_register_event_handler);

static void ubcore_dispatch_event_clients(struct ubcore_event *event)
{
	struct ubcore_event_handler *handler;
	struct ubcore_device *dev = event->ub_dev;

	down_read(&dev->event_handler_rwsem);
	list_for_each_entry(handler, &dev->event_handler_list, node)
		handler->event_callback(event, handler);
	up_read(&dev->event_handler_rwsem);
}

static void ubcore_dispatch_event_task(struct work_struct *work)
{
	struct ubcore_event_work *l_ubcore_event =
		container_of(work, struct ubcore_event_work, work);

	ubcore_dispatch_event_clients(&l_ubcore_event->event);
	kfree(l_ubcore_event);
}

int ubcore_dispatch_event(struct ubcore_event *event)
{
	struct ubcore_event_work *l_ubcore_event;

	l_ubcore_event = kzalloc(sizeof(*l_ubcore_event), GFP_ATOMIC);
	if (!l_ubcore_event)
		return -ENOMEM;

	INIT_WORK(&l_ubcore_event->work, ubcore_dispatch_event_task);
	l_ubcore_event->event = *event;

	if (ubcore_queue_work((int)UBCORE_DISPATCH_EVENT_WQ,
			      &l_ubcore_event->work) != 0) {
		kfree(l_ubcore_event);
		ubcore_log_err("Queue work failed");
	}

	return 0;
}

void ubcore_unregister_event_handler(struct ubcore_device *dev,
				     struct ubcore_event_handler *handler)
{
	if (dev == NULL || handler == NULL) {
		ubcore_log_err("Invalid argument.\n");
		return;
	}

	down_write(&dev->event_handler_rwsem);
	list_del(&handler->node);
	up_write(&dev->event_handler_rwsem);
}
EXPORT_SYMBOL(ubcore_unregister_event_handler);

static bool ubcore_preprocess_event(struct ubcore_event *event)
{
	if (event->event_type == UBCORE_EVENT_TP_ERR &&
	    event->element.tp != NULL) {
		ubcore_log_info("ubcore detect tp error event with tpn %u",
				event->element.tp->tpn);
		if (event->ub_dev->transport_type == UBCORE_TRANSPORT_UB) {
			if (event->element.tp->state == UBCORE_TP_STATE_ERR ||
			    event->element.tp->state == UBCORE_TP_STATE_RESET) {
				ubcore_log_warn(
					"Tp %u already in state %d, ignore err event",
					event->element.tp->tpn,
					(int32_t)event->element.tp->state);
				return true;
			}
		}
		return true;
	} else if (event->event_type == UBCORE_EVENT_TP_FLUSH_DONE &&
		   event->element.tp != NULL) {
		/* Scenarios of flush done
		 * 1. tp err: ubcore informs lower layer change tp to ERR when tp err.
		 * It triggers udma to flush after udma change tp to ERR.
		 * udma reports flush done event after flush done.
		 * 2. qpc err: udma senses qpc err, flushes and reports flush done event.
		 * tp has not been changed to ERR in this case.
		 * ubcore needs to change tp to ERR before change it to RESET.
		 */
		ubcore_log_info("ubcore detect tp %u flush done event",
				event->element.tp->tpn);
		if (event->element.tp->state == UBCORE_TP_STATE_RESET) {
			ubcore_log_warn(
				"Tp %u already in state %d, ignore flush done event",
				event->element.tp->tpn,
				(int32_t)event->element.tp->state);
			return true;
		}
	}
	return false;
}

void ubcore_dispatch_async_event(struct ubcore_event *event)
{
	if (event == NULL || event->ub_dev == NULL) {
		ubcore_log_err("Invalid argument.\n");
		return;
	}

	if (ubcore_preprocess_event(event))
		return;

	if (ubcore_dispatch_event(event) != 0)
		ubcore_log_err("ubcore_dispatch_event failed");
}
EXPORT_SYMBOL(ubcore_dispatch_async_event);

bool ubcore_eid_valid(struct ubcore_device *dev, uint32_t eid_index,
		      struct ubcore_udata *udata)
{
	/* For user space */
	if (udata != NULL) {
		/* uctx must be set */
		if (udata->uctx == NULL) {
			ubcore_log_err("Invalid parameter.\n");
			return false;
		}

		/* compare uctx->eid_index with the given eid_index */
		if (udata->uctx->eid_index != eid_index) {
			ubcore_log_err(
				"eid_indx: %u is consistent with the eid_indx: %u in uctx.\n",
				eid_index, udata->uctx->eid_index);
			return false;
		}
	} else {
		/* For kernel space */
		/* Check if given eid_idx exists without checking ns,
		 * as the current->nsproxy->net_ns can be changed.
		 */
		if (eid_index >= dev->eid_table.eid_cnt) {
			ubcore_log_err("eid_indx: %u is over the up limit: %u",
				       eid_index, dev->eid_table.eid_cnt);
			return false;
		}

		spin_lock(&dev->eid_table.lock);
		if (IS_ERR_OR_NULL(dev->eid_table.eid_entries)) {
			spin_unlock(&dev->eid_table.lock);
			return false;
		}
		if (!dev->eid_table.eid_entries[eid_index].valid) {
			spin_unlock(&dev->eid_table.lock);
			return false;
		}
		spin_unlock(&dev->eid_table.lock);
	}
	return true;
}

bool ubcore_eid_accessible(struct ubcore_device *dev, uint32_t eid_index)
{
	struct net *net;

	if (eid_index >= dev->eid_table.eid_cnt) {
		ubcore_log_err("eid_indx: %u is over the up limit: %u",
			       eid_index, dev->eid_table.eid_cnt);
		return false;
	}

	spin_lock(&dev->eid_table.lock);
	if (IS_ERR_OR_NULL(dev->eid_table.eid_entries)) {
		spin_unlock(&dev->eid_table.lock);
		return false;
	}

	if (!dev->eid_table.eid_entries[eid_index].valid) {
		spin_unlock(&dev->eid_table.lock);
		return false;
	}
	net = dev->eid_table.eid_entries[eid_index].net;
	spin_unlock(&dev->eid_table.lock);
	return net_eq(net, current->nsproxy->net_ns);
}

void ubcore_clear_pattern1_eid(struct ubcore_device *dev, union ubcore_eid *eid)
{
	struct ubcore_ueid_cfg cfg;
	uint32_t eid_idx = 0;

	if (ubcore_update_eidtbl_by_eid(dev, eid, &eid_idx, false, NULL) != 0)
		return;

	cfg.eid = *eid;
	cfg.eid_index = eid_idx;
	cfg.upi = 0;
	(void)ubcore_delete_ueid(dev, dev->attr.ue_idx, &cfg);
}

void ubcore_clear_pattern3_eid(struct ubcore_device *dev, union ubcore_eid *eid)
{
	struct ubcore_ueid_cfg cfg;
	uint32_t pattern3_upi = 0;
	uint32_t eid_idx = 0;

	if (ubcore_update_eidtbl_by_eid(dev, eid, &eid_idx, false, NULL) != 0)
		return;

	if (pattern3_upi != (uint32_t)UCBORE_INVALID_UPI) {
		cfg.eid = *eid;
		cfg.eid_index = eid_idx;
		cfg.upi = pattern3_upi;
		(void)ubcore_delete_ueid(dev, dev->attr.ue_idx, &cfg);
	} else {
		ubcore_log_err("upi not configured\n");
	}
}

int ubcore_process_mue_update_eid_tbl_notify_msg(struct ubcore_device *dev,
						 struct ubcore_resp *resp)
{
	struct ubcore_update_eid_tbl_notify *msg_notify =
		(struct ubcore_update_eid_tbl_notify *)resp->data;
	struct ubcore_event event = { 0 };
	struct net *net = &init_net;
	union ubcore_eid eid;
	int ret = 0;
	uint32_t i;

	if (dev->eid_table.eid_entries == NULL) {
		ubcore_log_err("eid_table is NULL\n");
		ret = -EINVAL;
		goto free_resp;
	}

	// If in dynamic eid mode, flush it and change to static eid mode.
	if (dev->dynamic_eid) {
		event.ub_dev = dev;
		event.event_type = UBCORE_EVENT_EID_CHANGE;
		for (i = 0; i < dev->attr.dev_cap.max_eid_cnt; i++) {
			if (dev->eid_table.eid_entries[i].valid == true) {
				eid = dev->eid_table.eid_entries[i].eid;
				if (dev->attr.pattern ==
				    (uint8_t)UBCORE_PATTERN_1)
					ubcore_clear_pattern1_eid(dev, &eid);
				else
					ubcore_clear_pattern3_eid(dev, &eid);
				event.element.eid_idx = i;
				ubcore_dispatch_async_event(&event);
			}
		}
		dev->dynamic_eid = false;
	}

	if (msg_notify->is_alloc_eid)
		net = read_pnet(&dev->ldev.net);

	if (ubcore_update_eidtbl_by_idx(dev, &msg_notify->eid,
					msg_notify->eid_idx,
					msg_notify->is_alloc_eid, net) != 0) {
		ubcore_log_err("ubcore_update_eidtbl_by_idx fail\n");
		ret = -1;
		goto free_resp;
	}

free_resp:
	kfree(resp);
	return ret;
}

bool ubcore_dev_accessible(struct ubcore_device *dev, struct net *net)
{
	return (g_shared_ns || net_eq(net, read_pnet(&dev->ldev.net)));
}

struct ubcore_ucontext *
ubcore_alloc_ucontext(struct ubcore_device *dev, uint32_t eid_index,
		      struct ubcore_udrv_priv *udrv_data)
{
	struct ubcore_ucontext *ucontext;
	struct ubcore_cg_object cg_obj;
	int ret;

	if (dev == NULL ||
	    strnlen(dev->dev_name, UBCORE_MAX_DEV_NAME) >=
		    UBCORE_MAX_DEV_NAME ||
	    dev->ops == NULL || dev->ops->alloc_ucontext == NULL ||
	    eid_index >= UBCORE_MAX_EID_CNT) {
		ubcore_log_err("Invalid argument.\n");
		return ERR_PTR(-EINVAL);
	}

	if (!ubcore_dev_accessible(dev, current->nsproxy->net_ns) ||
	    !ubcore_eid_accessible(dev, eid_index)) {
		ubcore_log_err("eid is not accessible by current ns.\n");
		return ERR_PTR(-EPERM);
	}

	ret = ubcore_cgroup_try_charge(&cg_obj, dev,
				       UBCORE_RESOURCE_HCA_HANDLE);
	if (ret != 0) {
		ubcore_log_err("cgroup charge fail:%d ,dev_name :%s\n", ret,
			       dev->dev_name);
		return ERR_PTR(ret);
	}

	ucontext = dev->ops->alloc_ucontext(dev, eid_index, udrv_data);
	if (IS_ERR_OR_NULL(ucontext)) {
		ubcore_log_err("failed to alloc ucontext.\n");
		ubcore_cgroup_uncharge(&cg_obj, dev,
				       UBCORE_RESOURCE_HCA_HANDLE);
		return UBCORE_CHECK_RETURN_ERR_PTR(ucontext, ENOEXEC);
	}

	ucontext->eid_index = eid_index;
	ucontext->ub_dev = dev;
	ucontext->cg_obj = cg_obj;

	return ucontext;
}
EXPORT_SYMBOL(ubcore_alloc_ucontext);

void ubcore_free_ucontext(struct ubcore_device *dev,
			  struct ubcore_ucontext *ucontext)
{
	int ret;
	struct ubcore_cg_object cg_obj;

	ubcore_log_info("Start free ucontext, dev ptr: %p, ucontext ptr: %p.\n",
			dev, ucontext);
	if (dev == NULL || ucontext == NULL || dev->ops == NULL ||
	    dev->ops->free_ucontext == NULL) {
		if (dev != NULL) {
			ubcore_log_info("dev->ops ptr: %p.\n", dev->ops);
			if (dev->ops != NULL)
				ubcore_log_info(
					"dev->ops->free_ucontext ptr: %p.\n",
					dev->ops->free_ucontext);
		}
		ubcore_log_err("Invalid argument.\n");
		return;
	}
	cg_obj = ucontext->cg_obj;

	ret = dev->ops->free_ucontext(ucontext);
	if (ret != 0)
		ubcore_log_err("failed to free_adu, ret: %d.\n", ret);

	ubcore_cgroup_uncharge(&cg_obj, dev, UBCORE_RESOURCE_HCA_HANDLE);
}
EXPORT_SYMBOL(ubcore_free_ucontext);

int ubcore_add_ueid(struct ubcore_device *dev, uint16_t ue_idx,
		    struct ubcore_ueid_cfg *cfg)
{
	int ret;

	if (dev == NULL || cfg == NULL || dev->ops == NULL ||
	    ue_idx >= UBCORE_MAX_UE_CNT) {
		ubcore_log_err("Invalid argument.\n");
		return -EINVAL;
	}

	if (dev->ops->add_ueid == NULL)
		return 0;

	ret = dev->ops->add_ueid(dev, ue_idx, cfg);
	if (ret != 0)
		ubcore_log_err("failed to add ueid, ue_idx:%hu, eid:" EID_FMT
			       ", upi:%u, eid_idx:%u, ret:%d\n",
			       ue_idx, EID_ARGS(cfg->eid), cfg->upi,
			       cfg->eid_index, ret);
	else
		ubcore_log_info("success to add ueid, ue_idx:%hu, eid:" EID_FMT
				", upi:%u, eid_idx:%u, ret:%d\n",
				ue_idx, EID_ARGS(cfg->eid), cfg->upi,
				cfg->eid_index, ret);

	return ret;
}
EXPORT_SYMBOL(ubcore_add_ueid);

int ubcore_delete_ueid(struct ubcore_device *dev, uint16_t ue_idx,
		       struct ubcore_ueid_cfg *cfg)
{
	int ret;

	if (dev == NULL || cfg == NULL || dev->ops == NULL ||
	    ue_idx >= UBCORE_MAX_UE_CNT) {
		ubcore_log_err("Invalid argument.\n");
		return -EINVAL;
	}

	if (dev->ops->delete_ueid == NULL)
		return 0;

	ret = dev->ops->delete_ueid(dev, ue_idx, cfg);
	if (ret != 0)
		ubcore_log_err("failed to del ueid, ue_idx:%hu, eid:" EID_FMT
			       ", upi:%u, eid_idx:%u, ret:%d\n",
			       ue_idx, EID_ARGS(cfg->eid), cfg->upi,
			       cfg->eid_index, ret);
	else
		ubcore_log_info("success to del ueid, ue_idx:%hu, eid:" EID_FMT
				", upi:%u, eid_idx:%u, ret:%d\n",
				ue_idx, EID_ARGS(cfg->eid), cfg->upi,
				cfg->eid_index, ret);

	return ret;
}
EXPORT_SYMBOL(ubcore_delete_ueid);

int ubcore_query_device_attr(struct ubcore_device *dev,
			     struct ubcore_device_attr *attr)
{
	int ret;

	if (dev == NULL || attr == NULL || dev->ops == NULL ||
	    dev->ops->query_device_attr == NULL) {
		ubcore_log_err("Invalid argument.\n");
		return -EINVAL;
	}

	ret = dev->ops->query_device_attr(dev, attr);
	if (ret != 0) {
		ubcore_log_err("failed to query device attr, ret: %d.\n", ret);
		return -EPERM;
	}
	return 0;
}
EXPORT_SYMBOL(ubcore_query_device_attr);

int ubcore_query_device_status(struct ubcore_device *dev,
			       struct ubcore_device_status *status)
{
	int ret;

	if (dev == NULL || status == NULL || dev->ops == NULL ||
	    dev->ops->query_device_status == NULL) {
		ubcore_log_err("Invalid argument.\n");
		return -EINVAL;
	}

	ret = dev->ops->query_device_status(dev, status);
	if (ret != 0) {
		ubcore_log_err("failed to query device status, ret: %d.\n",
			       ret);
		return -EPERM;
	}
	return 0;
}
EXPORT_SYMBOL(ubcore_query_device_status);

int ubcore_query_resource(struct ubcore_device *dev, struct ubcore_res_key *key,
			  struct ubcore_res_val *val)
{
	int ret;

	if (dev == NULL || key == NULL || val == NULL || dev->ops == NULL ||
	    dev->ops->query_res == NULL) {
		ubcore_log_err("Invalid argument.\n");
		return -EINVAL;
	}
	ret = dev->ops->query_res(dev, key, val);
	if (ret != 0) {
		ubcore_log_err("failed to query res, ret: %d.\n", ret);
		return -EPERM;
	}
	return 0;
}
EXPORT_SYMBOL(ubcore_query_resource);

int ubcore_config_device(struct ubcore_device *dev,
			 struct ubcore_device_cfg *cfg)
{
	int ret;

	if (dev == NULL || cfg == NULL || dev->ops == NULL ||
	    dev->ops->config_device == NULL) {
		ubcore_log_err("Invalid argument.\n");
		return -EINVAL;
	}

	ret = dev->ops->config_device(dev, cfg);
	if (ret != 0) {
		ubcore_log_err("failed to config device, ret: %d.\n", ret);
		return -EPERM;
	}
	return 0;
}
EXPORT_SYMBOL(ubcore_config_device);

int ubcore_user_control(struct ubcore_device *dev,
			struct ubcore_user_ctl *k_user_ctl)
{
	int ret;

	if (k_user_ctl == NULL) {
		ubcore_log_err("invalid parameter with input nullptr.\n");
		return -1;
	}

	if (dev == NULL || dev->ops == NULL || dev->ops->user_ctl == NULL) {
		ubcore_log_err("invalid parameter with dev nullptr.\n");
		return -1;
	}

	ret = dev->ops->user_ctl(dev, k_user_ctl);
	if (ret != 0) {
		ubcore_log_err("failed to exec kdrv_user_ctl.\n");
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL(ubcore_user_control);

int ubcore_query_stats(struct ubcore_device *dev, struct ubcore_stats_key *key,
		       struct ubcore_stats_val *val)
{
	int ret;

	if (dev == NULL || key == NULL || val == NULL || dev->ops == NULL ||
	    dev->ops->query_stats == NULL) {
		ubcore_log_err("Invalid argument.\n");
		return -EINVAL;
	}

	ret = dev->ops->query_stats(dev, key, val);
	if (ret != 0) {
		ubcore_log_err("Failed to query stats, ret: %d.\n", ret);
		return -EPERM;
	}
	return 0;
}
EXPORT_SYMBOL(ubcore_query_stats);

struct ubcore_eid_info *ubcore_get_eid_list(struct ubcore_device *dev,
					    uint32_t *cnt)
{
	struct ubcore_eid_info *eid_list;
	struct ubcore_eid_info *tmp;
	uint32_t count;
	uint32_t i;

	if (dev == NULL || dev->attr.dev_cap.max_eid_cnt == 0 ||
	    dev->attr.dev_cap.max_eid_cnt > UBCORE_MAX_EID_CNT || cnt == NULL ||
	    IS_ERR_OR_NULL(dev->eid_table.eid_entries) ||
	    strnlen(dev->dev_name, UBCORE_MAX_DEV_NAME) >=
		    UBCORE_MAX_DEV_NAME) {
		ubcore_log_err("invalid input parameter.\n");
		return NULL;
	}

	tmp = vmalloc(dev->attr.dev_cap.max_eid_cnt *
		      sizeof(struct ubcore_eid_info));
	if (tmp == NULL)
		return NULL;

	spin_lock(&dev->eid_table.lock);
	for (i = 0, count = 0; i < dev->attr.dev_cap.max_eid_cnt; i++) {
		if (dev->eid_table.eid_entries[i].valid == true) {
			tmp[count].eid = dev->eid_table.eid_entries[i].eid;
			tmp[count].eid_index = i;
			count++;
		}
	}
	spin_unlock(&dev->eid_table.lock);
	if (count == 0) {
		vfree(tmp);
		ubcore_log_warn("There is no eids in device: %s eid_table.\n",
				dev->dev_name);
		return NULL;
	}
	*cnt = count;

	eid_list = vmalloc(count * sizeof(struct ubcore_eid_info));
	if (eid_list == NULL) {
		vfree(tmp);
		return NULL;
	}
	for (i = 0; i < count; i++)
		eid_list[i] = tmp[i];

	vfree(tmp);
	return eid_list;
}
EXPORT_SYMBOL(ubcore_get_eid_list);

void ubcore_free_eid_list(struct ubcore_eid_info *eid_list)
{
	if (eid_list != NULL)
		vfree(eid_list);
}
EXPORT_SYMBOL(ubcore_free_eid_list);

static void ubcore_modify_eid_ns(struct ubcore_device *dev, struct net *net)
{
	struct ubcore_eid_entry *e;
	uint32_t i;

	if (dev->eid_table.eid_entries == NULL)
		return;

	spin_lock(&dev->eid_table.lock);
	for (i = 0; i < dev->eid_table.eid_cnt; i++) {
		e = &dev->eid_table.eid_entries[i];
		if (e->valid && !net_eq(e->net, net))
			e->net = net;
	}
	spin_unlock(&dev->eid_table.lock);
}

static void ubcore_invalidate_eid_ns(struct ubcore_device *dev, struct net *net)
{
	struct ubcore_eid_entry *e;
	uint32_t i;

	if (dev->eid_table.eid_entries == NULL)
		return;

	spin_lock(&dev->eid_table.lock);
	for (i = 0; i < dev->eid_table.eid_cnt; i++) {
		e = &dev->eid_table.eid_entries[i];
		if (e->valid && net_eq(e->net, net)) {
			e->net = &init_net;
			e->valid = false;
		}
	}
	spin_unlock(&dev->eid_table.lock);
}

static int ubcore_modify_dev_ns(struct ubcore_device *dev, struct net *net,
				bool exit)
{
	struct net *cur;
	int ret;

	cur = read_pnet(&dev->ldev.net);
	if (net_eq(net, cur))
		return 0;

	kobject_uevent(&dev->ldev.dev->kobj, KOBJ_REMOVE);
	ubcore_clients_remove(dev);
	write_pnet(&dev->ldev.net, net);
	ret = device_rename(dev->ldev.dev, dev_name(dev->ldev.dev));
	if (ret) {
		write_pnet(&dev->ldev.net, cur);
		ubcore_log_err("Failed to rename device in the new ns.\n");
		goto out;
	}

	if (exit)
		ubcore_invalidate_eid_ns(dev, cur);
	else
		ubcore_modify_eid_ns(dev, net);

out:
	ubcore_clients_add(dev);
	kobject_uevent(&dev->ldev.dev->kobj, KOBJ_ADD);
	return ret;
}

int ubcore_set_dev_ns(char *device_name, uint32_t ns_fd)
{
	struct ubcore_device *dev = NULL, *tmp;
	struct net *net;
	int ret = 0;

	if (g_shared_ns) {
		ubcore_log_err(
			"Can not set device to ns under shared ns mode.\n");
		return -EPERM;
	}

	net = get_net_ns_by_fd(ns_fd);
	if (IS_ERR(net)) {
		ubcore_log_err("Failed to get ns by fd.\n");
		return PTR_ERR(net);
	}

	/* Find device by name */
	/* device_name len checked by genl */
	down_read(&g_device_rwsem);
	list_for_each_entry(tmp, &g_device_list, list_node) {
		if (strcmp(dev_name(tmp->ldev.dev), device_name) == 0) {
			dev = tmp;
			break;
		}
	}
	if (dev == NULL || dev->transport_type != UBCORE_TRANSPORT_UB) {
		ret = -EINVAL;
		ubcore_log_err("Failed to find device.\n");
		goto out;
	}

	/* Put device in the new ns */
	ret = ubcore_modify_dev_ns(dev, net, false);

out:
	up_read(&g_device_rwsem);
	put_net(net);
	return ret;
}

int ubcore_set_ns_mode(bool shared)
{
	unsigned long flags;

	down_write(&g_ubcore_net_rwsem);
	if (g_shared_ns == shared) {
		up_write(&g_ubcore_net_rwsem);
		return 0;
	}
	spin_lock_irqsave(&g_ubcore_net_lock, flags);
	if (!list_empty(&g_ubcore_net_list)) {
		spin_unlock_irqrestore(&g_ubcore_net_lock, flags);
		up_write(&g_ubcore_net_rwsem);
		ubcore_log_err("Failed to modify ns mode with existing ns");
		return -EPERM;
	}
	g_shared_ns = shared;
	spin_unlock_irqrestore(&g_ubcore_net_lock, flags);
	up_write(&g_ubcore_net_rwsem);
	return 0;
}

void ubcore_net_exit(struct net *net)
{
	struct ubcore_net *unet = net_generic(net, g_ubcore_net_id);
	struct ubcore_device *dev;
	unsigned long flags;

	if (unet == NULL)
		return;

	ubcore_log_info("net exit %u, net:0x%p", net->ns.inum, net);
	down_write(&g_ubcore_net_rwsem);
	spin_lock_irqsave(&g_ubcore_net_lock, flags);
	if (list_empty(&unet->node)) {
		spin_unlock_irqrestore(&g_ubcore_net_lock, flags);
		up_write(&g_ubcore_net_rwsem);
		return;
	}
	list_del_init(&unet->node);
	spin_unlock_irqrestore(&g_ubcore_net_lock, flags);
	up_write(&g_ubcore_net_rwsem);

	if (!g_shared_ns) {
		down_read(&g_device_rwsem);
		list_for_each_entry(dev, &g_device_list, list_node) {
			if (dev->transport_type != UBCORE_TRANSPORT_UB ||
			    !net_eq(read_pnet(&dev->ldev.net), net))
				continue;
			(void)ubcore_modify_dev_ns(dev, &init_net, true);
		}
		up_read(&g_device_rwsem);
	} else {
		down_write(&g_device_rwsem);
		list_for_each_entry(dev, &g_device_list, list_node) {
			if (dev->transport_type != UBCORE_TRANSPORT_UB)
				continue;
			ubcore_remove_one_logic_device(dev, net);
			ubcore_invalidate_eid_ns(dev, net);
		}
		up_write(&g_device_rwsem);
	}
}

static int ubcore_net_init(struct net *net)
{
	struct ubcore_net *unet = net_generic(net, g_ubcore_net_id);
	struct ubcore_device *dev;
	unsigned long flags;
	int ret = 0;

	if (unet == NULL)
		return 0;

	ubcore_log_info("net init %u, net:0x%p", net->ns.inum, net);
	write_pnet(&unet->net, net);
	if (net_eq(net, &init_net)) {
		INIT_LIST_HEAD(&unet->node);
		return 0;
	}

	spin_lock_irqsave(&g_ubcore_net_lock, flags);
	list_add_tail(&unet->node, &g_ubcore_net_list);
	spin_unlock_irqrestore(&g_ubcore_net_lock, flags);

	if (!g_shared_ns)
		return 0;

	down_read(&g_device_rwsem);
	list_for_each_entry(dev, &g_device_list, list_node) {
		if (dev->transport_type != UBCORE_TRANSPORT_UB)
			continue;

		down_read(&g_ubcore_net_rwsem);
		ret = ubcore_add_one_logic_device(dev, net);
		up_read(&g_ubcore_net_rwsem);
		if (ret)
			break;
	}
	up_read(&g_device_rwsem);
	if (ret)
		ubcore_net_exit(net);

	/* return ret will cause error starting a container */
	return 0;
}

static struct pernet_operations g_ubcore_net_ops = {
	.init = ubcore_net_init,
	.exit = ubcore_net_exit,
	.id = &g_ubcore_net_id,
	.size = sizeof(struct ubcore_net)
};

int ubcore_register_pnet_ops(void)
{
	return register_pernet_device(&g_ubcore_net_ops);
}
void ubcore_unregister_pnet_ops(void)
{
	unregister_pernet_device(&g_ubcore_net_ops);
}

void ubcore_dispatch_mgmt_event(struct ubcore_mgmt_event *event)
{
	struct ubcore_eid_info *eid_info;
	struct net *net = &init_net;
	int ret;

	if (event == NULL || event->ub_dev == NULL ||
	    event->element.eid_info == NULL) {
		ubcore_log_err("Invalid parameter.\n");
		return;
	}
	eid_info = event->element.eid_info;

	switch (event->event_type) {
	case UBCORE_MGMT_EVENT_EID_ADD:
		net = read_pnet(&event->ub_dev->ldev.net);
		ret = ubcore_update_eidtbl_by_idx(event->ub_dev, &eid_info->eid,
						  eid_info->eid_index, true,
						  net);
		break;
	case UBCORE_MGMT_EVENT_EID_RMV:
		ret = ubcore_update_eidtbl_by_idx(event->ub_dev, &eid_info->eid,
						  eid_info->eid_index, false,
						  net);
		break;
	default:
		ubcore_log_err("Invalid event_type: %d.\n", event->event_type);
		return;
	}

	if (ret != 0)
		ubcore_log_err(
			"Failed to update eid table, index: %u, type: %d.\n",
			eid_info->eid_index, event->event_type);
}
EXPORT_SYMBOL(ubcore_dispatch_mgmt_event);

struct ubcore_device *ubcore_get_device_by_eid(union ubcore_eid *eid,
					       enum ubcore_transport_type type)
{
	struct ubcore_device *dev, *target = NULL;
	uint32_t idx;

	if (eid == NULL || type >= UBCORE_TRANSPORT_MAX) {
		ubcore_log_err("Invalid parameter.\n");
		return NULL;
	}

	down_read(&g_device_rwsem);
	list_for_each_entry(dev, &g_device_list, list_node) {
		if (IS_ERR_OR_NULL(dev->eid_table.eid_entries))
			continue;
		for (idx = 0; idx < dev->attr.dev_cap.max_eid_cnt; idx++) {
			if (memcmp(&dev->eid_table.eid_entries[idx].eid, eid,
				   sizeof(union ubcore_eid)) == 0 &&
			    dev->transport_type == type) {
				target = dev;
				break;
			}
		}
		if (target != NULL)
			break;
	}
	up_read(&g_device_rwsem);
	return target;
}
EXPORT_SYMBOL(ubcore_get_device_by_eid);

>>>>>>> 4023540b983e (ubcore: implement support for UVS commands)

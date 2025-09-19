// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) HiSilicon Technologies Co., Ltd. 2025. All rights reserved.
 */

#define pr_fmt(fmt)	"ubus port: " fmt

#include "ubus.h"
#include "port.h"

struct ub_port_attribute {
	struct attribute attr;
	ssize_t (*show)(struct ub_port *port, char *buf);
	ssize_t (*store)(struct ub_port *port, const char *buf, size_t count);
};

#define to_ub_port(o) container_of(o, struct ub_port, kobj)
#define to_ub_port_attr(a) container_of(a, struct ub_port_attribute, attr)

static const struct attribute_group *ub_port_groups[] = {
	NULL
};

static ssize_t ub_port_attr_show(struct kobject *kobj, struct attribute *attr,
				 char *buf)
{
	struct ub_port_attribute *attribute = to_ub_port_attr(attr);
	struct ub_port *port = to_ub_port(kobj);

	if (!attribute->show)
		return -EIO;

	return attribute->show(port, buf);
}

static ssize_t ub_port_attr_store(struct kobject *kobj, struct attribute *attr,
				  const char *buf, size_t count)
{
	struct ub_port_attribute *attribute = to_ub_port_attr(attr);
	struct ub_port *port = to_ub_port(kobj);

	if (!attribute->store)
		return -EIO;

	return attribute->store(port, buf, count);
}

static const struct sysfs_ops ub_port_sysfs_ops = {
	.show = ub_port_attr_show,
	.store = ub_port_attr_store,
};

static const struct kobj_type ub_port_ktype = {
	.sysfs_ops = &ub_port_sysfs_ops,
	.default_groups = ub_port_groups,
};

void ub_port_disconnect(struct ub_port *port)
{
	struct ub_port *r_port;

	if (!port || !port->r_uent)
		return;

	r_port = port->r_uent->ports + port->r_index;
	r_port->r_uent = NULL;
	r_port->r_guid = guid_null;

	port->r_uent = NULL;
	port->r_guid = guid_null;
}

/* connect two ports, caller should make sure that the ports match */
void ub_port_connect(struct ub_port *port, struct ub_port *r_port)
{
	if (!port || !r_port)
		return;

	port->r_index = r_port->index;
	port->r_uent = r_port->uent;
	r_port->r_index = port->index;
	r_port->r_uent = port->uent;
}

bool ub_check_and_connect(struct ub_port *port, struct ub_entity *r_uent)
{
	struct ub_port *r_port;

	if (!port || !r_uent)
		return false;

	if (port->r_index >= r_uent->port_nums) {
		pr_err("port%u should connect to port%u, but remote device has only %u ports\n",
		       port->index, port->r_index, r_uent->port_nums);
		return false;
	}

	r_port = r_uent->ports + port->r_index;

	if (r_port->r_uent) {
		pr_err("port%u should connect to port%u, which is already connected\n",
		       port->index, port->r_index);
		return false;
	}

	ub_port_connect(port, r_port);

	return true;
}

static int ub_port_config(struct ub_port *port)
{
	return 0;
}

int ub_ports_add(struct ub_entity *uent)
{
	struct ub_port *port;
	int ret;

	if (!uent)
		return -EINVAL;

	for_each_uent_port(port, uent) {
		ret = ub_port_config(port);
		if (ret) {
			ub_err(uent,
			       "config port%u failed, stop adding ports\n",
			       port->index);
			return ret;
		}

		ret = kobject_add(&port->kobj, &uent->dev.kobj, "port%u",
				  port->index);
		if (ret) {
			ub_warn(uent, "cannot add port%u, stop adding ports\n",
				port->index);
			return ret;
		}
	}

	return 0;
}

void ub_ports_del(struct ub_entity *uent)
{
	struct ub_port *port;

	if (!uent)
		return;

	for_each_uent_port(port, uent)
		kobject_put(&port->kobj);
}

static void ub_port_init(struct ub_entity *uent, struct ub_port *port)
{
	port->uent = uent;
	port->type = PHYSICAL;
	port->cna = 0;
	port->r_uent = NULL;
	port->r_index = 0;
	port->r_guid = guid_null;
	bitmap_zero(port->cna_maps, UB_MAX_CNA_NUM);
	bitmap_zero(port->cap_map, UB_PORT_CAP_NUM);
	kobject_init(&port->kobj, &ub_port_ktype);
}

int ub_ports_setup(struct ub_entity *uent)
{
	struct ub_port *port;

	if (!uent || !uent->port_nums)
		return -EINVAL;

	if (uent->ports)
		return 0;

	uent->ports = kvcalloc(uent->port_nums, sizeof(*port), GFP_KERNEL);
	if (!uent->ports)
		return -ENOMEM;

	for_each_uent_port(port, uent) {
		port->index = port - uent->ports;
		ub_port_init(uent, port);
	}

	return 0;
}

void ub_ports_unset(struct ub_entity *uent)
{
	if (!uent)
		return;

	kvfree(uent->ports);
	uent->ports = NULL;
	pr_debug("port release\n");
}

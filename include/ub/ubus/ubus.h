/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) HiSilicon Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef _UB_UBUS_UBUS_H_
#define _UB_UBUS_UBUS_H_

#include <linux/device.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/types.h>
#include <uapi/ub/ubus/ubus_regs.h>
#include <ub/ubus/ubus_ids.h>
#include <linux/mod_devicetable.h>

#define UB_ENTITY(v, d) \
	.vendor = (v), .device = (d), \
	.mod_vendor = (u32)UB_ANY_ID, .module = (u32)UB_ANY_ID

#define UB_ENTITY_MODULE(v, d, m_v, m) \
	.vendor = (v), .device = (d), \
	.mod_vendor = (m_v), .module = (m)

#define UB_ENTITY_CLASS(dev_class, dev_class_mask) \
	.vendor = (u32)UB_ANY_ID, .device = (u32)UB_ANY_ID, \
	.mod_vendor = (u32)UB_ANY_ID, .module = (u32)UB_ANY_ID, \
	.class_code = (dev_class), .class_mask = (dev_class_mask)

#define UB_GUID_DW_NUM SZ_4

struct ub_guid {
	union {
		struct {
			unsigned long long seq_num;
			unsigned int reserved : 24;
			unsigned int type : 4;
			unsigned int version : 4;
			unsigned int device : 16;
			unsigned int vendor : 16;
		} bits;

		guid_t id;

		unsigned int dw[UB_GUID_DW_NUM];
	};
};

#define uent_vendor(uent) ((uent)->guid.bits.vendor)
#define uent_type(uent) ((uent)->guid.bits.type)
#define uent_version(uent) ((uent)->guid.bits.version)
#define uent_device(uent) ((uent)->guid.bits.device)
#define uent_class(uent) ((uent)->class_code)
#define uent_base_code(uent) ((uent)->class_code & UB_BASE_CODE_MASK)
#define uent_seq(uent) ((uent)->guid.bits.seq_num)

static inline int ub_show_guid(struct ub_guid *guid, char *buf)
{
	return sprintf(buf, "%04x-%04x-%01x-%01x-%06x-%016llx",
		       guid->bits.vendor, guid->bits.device,
		       guid->bits.version, guid->bits.type,
		       guid->bits.reserved, guid->bits.seq_num);
}

struct ub_entity {
	/* Driver framework base info */
	struct device dev;
	struct ub_driver *driver;
	bool match_driver; /* Skip attaching driver before dev ready */
	const char *driver_override; /* Driver name to force a match */

	/* entity base info */
	struct ub_guid guid;
	u16 class_code;
	u16 mod_vendor; /* entity's module vendor and module id */
	u16 module;
	unsigned int cna;

	/* entity topology info */
	struct ub_bus_controller *ubc;
};

struct ub_dynids {
	spinlock_t lock; /* Protects list, index */
	struct list_head list; /* For IDs added at runtime */
};

#define to_ub_entity(n) container_of(n, struct ub_entity, dev)

/**
 * struct ub_driver - UB driver structure
 * @node:	List of driver structures.
 * @name:	Driver name.
 * @id_table:	Pointer to table of device IDs the driver is
 *		interested in.  Most drivers should export this
 *		table using MODULE_DEVICE_TABLE(ub,...).
 * @probe:	This probing function gets called (during execution
 *		of ub_register_driver() for already existing
 *		entities or later if a new entity gets inserted) for
 *		all UB entities which match the ID table and are not
 *		"owned" by the other drivers yet. This function gets
 *		passed a "struct ub_entity *" for each entity whose
 *		entry in the ID table matches the entity. The probe
 *		function returns zero when the driver chooses to
 *		take "ownership" of the entity or an error code
 *		(negative number) otherwise.
 *		The probe function always gets called from process
 *		context, so it can sleep.
 * @remove:	The remove() function gets called whenever an entity
 *		being handled by this driver is removed (either during
 *		deregistration of the driver or when it's manually
 *		removed from a hot-pluggable slot).
 *		The remove function always gets called from process
 *		context, so it can sleep.
 * @shutdown:	Hook into reboot_notifier_list (kernel/sys.c).
 *		Intended to stop any idling operations.
 * @groups:	Sysfs attribute groups.
 * @dev_groups: Attributes attached to the device that will be
 *		created once it is bound to the driver.
 * @driver:	Driver model structure.
 * @dynids:	List of dynamically added device IDs.
 */
struct ub_driver {
	struct list_head node;
	const char *name;
	const struct ub_device_id *id_table; /* Must be non-NULL for probe to be called */
	/* New entity inserted */
	int (*probe)(struct ub_entity *uent, const struct ub_device_id *id);
	/* entity removed (NULL if not a hot-plug capable driver) */
	void (*remove)(struct ub_entity *uent);
	void (*shutdown)(struct ub_entity *uent);
	const struct attribute_group **groups;
	const struct attribute_group **dev_groups;
	struct device_driver driver;
	struct ub_dynids dynids;
};

struct ub_bus_controller {
	struct device dev;
	struct ub_entity *uent;

	u32 ctl_no;
	struct message_device *mdev;
	struct list_head node;
	struct list_head devs;
	struct ub_bus_controller_ops *ops;
};

static inline struct ub_driver *to_ub_driver(struct device_driver *drv)
{
	return drv ? container_of(drv, struct ub_driver, driver) : NULL;
}

#define ub_err(pue, fmt, arg...)	dev_err(&(pue)->dev, fmt, ##arg)
#define ub_info(pue, fmt, arg...)	dev_info(&(pue)->dev, fmt, ##arg)
#define ub_warn(pue, fmt, arg...)	dev_warn(&(pue)->dev, fmt, ##arg)
#define ub_dbg(pue, fmt, arg...)	dev_dbg(&(pue)->dev, fmt, ##arg)

static inline const char *ub_name(const struct ub_entity *pue)
{
	return dev_name(&pue->dev);
}

#ifdef CONFIG_UB_UBUS
extern struct bus_type ub_bus_type;
#define dev_is_ub(d) ((d)->bus == &ub_bus_type)

/**
 * ub_entity_get() - Atomically increment the reference count for the entity.
 * @uent: UB entity pointer.
 *
 * Context: Any context.
 * Return: uent, or NULL if @uent is NULL.
 */
struct ub_entity *ub_entity_get(struct ub_entity *uent);

/**
 * ub_entity_put() - decrement the reference count for the entity.
 * @uent: UB entity pointer.
 *
 * Context: Any context.
 */
void ub_entity_put(struct ub_entity *uent);

/**
 * ub_get_bus_controller() - Obtains the ub bus controller entity list.
 * @uents: Output buffer for the UBC entities list.
 * @max_num: Buffer size.
 * @real_num: Real entities num.
 *
 * All ub bus controllers in the system are returned. Increase the reference
 * counting of all entities by 1. Remember to call ub_put_bus_controller() after
 * using it.
 *
 * Context: Any context.
 * Return: 0 if success, or %-EINVAL if input parameter is NULL,
 * or %-ENOMEM if buffer is too small.
 */
int ub_get_bus_controller(struct ub_entity *uents[], unsigned int max_num,
		      unsigned int *real_num);

/**
 * ub_put_bus_controller() - Free the ub bus controller device list.
 * @uents: UBC entities list
 * @num: entities num
 *
 * Decrement the reference counting of all entities by 1.
 *
 * Context: Any context.
 */
void ub_put_bus_controller(struct ub_entity *uents[], unsigned int num);

/* Proper probing supporting hot-pluggable entities */
int __ub_register_driver(struct ub_driver *drv, struct module *owner,
			 const char *mod_name);
/* ub_register_driver() must be a macro so KBUILD_MODNAME can be expanded */
#define ub_register_driver(driver) \
	__ub_register_driver(driver, THIS_MODULE, KBUILD_MODNAME)
void ub_unregister_driver(struct ub_driver *drv);

#else /* CONFIG_UB_UBUS is not enabled */
#define dev_is_ub(d) (false)
static inline struct ub_entity *ub_entity_get(struct ub_entity *uent)
{ return NULL; }
static inline void ub_entity_put(struct ub_entity *uent) {}
static inline int ub_get_bus_controller(struct ub_entity *uents[],
				    unsigned int max_num,
				    unsigned int *real_num)
{ return -ENODEV; }
static inline void
ub_put_bus_controller(struct ub_entity *uents[], unsigned int num) {}
static inline int
__ub_register_driver(struct ub_driver *drv, struct module *owner,
		     const char *mod_name)
{ return 0; }
static inline int ub_register_driver(struct ub_driver *drv)
{ return 0; }
static inline void ub_unregister_driver(struct ub_driver *drv) {}
#endif /* CONFIG_UB_UBUS */

#endif /* _UB_UBUS_UBUS_H_ */

/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) HiSilicon Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef _UB_UBUS_UBUS_H_
#define _UB_UBUS_UBUS_H_

#include <linux/device.h>
#include <linux/init.h>
#include <linux/iommu.h>
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

enum ub_type {
	UB_TYPE_BUS_INSTANCE = 0,
	UB_TYPE_CONTROLLER = 1,
	UB_TYPE_ICONTROLLER = 2,
	UB_TYPE_SWITCH = 3,
	UB_TYPE_ISWITCH = 4
};

static inline int ub_show_guid(struct ub_guid *guid, char *buf)
{
	return sprintf(buf, "%04x-%04x-%01x-%01x-%06x-%016llx",
		       guid->bits.vendor, guid->bits.device,
		       guid->bits.version, guid->bits.type,
		       guid->bits.reserved, guid->bits.seq_num);
}

enum ub_port_type {
	PHYSICAL,
	VIRTUAL,
};

#define UB_MAX_CNA_NUM SZ_64K
#define UB_PORT_CAP_NUM SZ_256

struct ub_port {
	struct ub_entity *uent;
	u16 index;
	enum ub_port_type type;
	u8 domain_boundary;
	bool shareable;
	u32 cna;
	struct ub_entity *r_uent; /* If valid, also represent link up */
	u16 r_index;
	guid_t r_guid;
	struct kobject kobj;
	DECLARE_BITMAP(cna_maps, UB_MAX_CNA_NUM);
	/* cap cache */
	DECLARE_BITMAP(cap_map, UB_PORT_CAP_NUM);
};

struct ub_entity {
	/* Driver framework base info */
	struct device dev;
	struct ub_driver *driver;
	bool match_driver; /* Skip attaching driver before dev ready */
	const char *driver_override; /* Driver name to force a match */
	unsigned long priv_flags; /* Private flags for the UB driver */

	/* entity base info */
	int ent_type;
	struct ub_guid guid;
	u16 class_code;
	u16 mod_vendor; /* entity's module vendor and module id */
	u16 module;
	unsigned int cna;
	unsigned int eid;
	unsigned short entity_idx;
	u32 uent_num; /* ub dev number */

	/* entity topology info */
	struct ub_bus_controller *ubc;
	struct ub_entity *pue; /* ue/mue connected to their mue */
	int topo_rank; /* The levels of Breadth-First Search */

	/* entity port info */
	u16 port_nums;
	struct ub_port *ports;

	/* entity route info */
	struct list_head cna_list; /* store distance for cna in route table */

	struct dev_message *message;
	u32 support_feature;

	u16 upi;
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
 * @driver_managed_dma: Device driver doesn't use kernel DMA API for DMA.
 *		For most device drivers, no need to care about this flag
 *		as long as all DMAs are handled through the kernel DMA API.
 *		For some special ones, for example VFIO drivers, they know
 *		how to manage the DMA themselves and set this flag so that
 *		the IOMMU layer will allow them to setup and manage their
 *		own I/O address space.
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
	bool driver_managed_dma;
};

struct ubc_common_attr {
	u32 int_id_start;
	u32 int_id_end;
	u64 hpa_base;
	u64 hpa_size;
	u8 mem_size_limit;
	u8 dma_cca;
	u16 ummu_map;
	u16 proximity_domain;
	u64 queue_addr;
	u64 queue_size;
	u16 queue_depth;
	u16 msg_int;
	u8 msg_int_attr;
	u64 ubc_guid_low;
	u64 ubc_guid_high;
};

struct ub_bus_controller {
	struct device dev;
	struct ub_entity *uent;
	struct ubc_common_attr attr;
	u32 queue_virq;
	char name[16];

	u32 ctl_no;
	struct message_device *mdev;
	struct list_head resources;
	struct list_head node;
	struct list_head devs;
	struct ub_bus_controller_ops *ops;
	bool cluster;

	void *data;
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

/* interfaces for shareable port */
enum ub_port_event {
	UB_PORT_EVENT_LINK_DOWN,
	UB_PORT_EVENT_LINK_UP,
	UB_PORT_EVENT_RESET_PREPARE,
	UB_PORT_EVENT_RESET_DONE
};

struct ub_share_port_ops {
	void (*reset_prepare)(struct ub_entity *uent, u16 port_id);
	void (*reset_done)(struct ub_entity *uent, u16 port_id);
	void (*event_notify)(struct ub_entity *uent, u16 port_id, int event);
};

#ifdef CONFIG_UB_UBUS
extern struct bus_type ub_bus_type;
#define dev_is_ub(d) ((d)->bus == &ub_bus_type)

void ub_bus_type_iommu_ops_set(const struct iommu_ops *ops);
const struct iommu_ops *ub_bus_type_iommu_ops_get(void);

/**
 * ub_register_share_port() - Register a share port.
 * @uent: UB entity.
 * @port_id: UB Bus Controller port id.
 * @ops: UB share port ops.
 *
 * The IDEV reuses the physical port of the ub bus controller. Record the
 * callback function.
 *
 * Context: Any context
 * Return: 0 if success, or %-ENOMEM if system out of memory,
 * or %-EINVAL if parameters invalid.
 */
int ub_register_share_port(struct ub_entity *uent, u16 port_id,
			   struct ub_share_port_ops *ops);

/**
 * ub_unregister_share_port() - Unregister a share port.
 * @uent: UB entity.
 * @port_id: UB Bus Controller port id.
 * @ops: UB share port ops.
 *
 * Clear the callback function.
 *
 * Context: Any context
 */
void ub_unregister_share_port(struct ub_entity *uent, u16 port_id,
			      struct ub_share_port_ops *ops);

/**
 * ub_cfg_read_byte() - 1 byte configuration access read.
 * @uent: UB entity.
 * @pos: Config space address.
 * @val: Output buffer.
 *
 * Initiate configuration access to the specified address of the entity
 * configuration space and read 1 byte.
 *
 * Context: Any context, It will take spin_lock_irqsave()/spin_unlock_restore()
 * Return: 0 if success, or negative value if failed.
 */
int ub_cfg_read_byte(struct ub_entity *uent, u64 pos, u8 *val);
int ub_cfg_read_word(struct ub_entity *uent, u64 pos, u16 *val);
int ub_cfg_read_dword(struct ub_entity *uent, u64 pos, u32 *val);
/**
 * ub_cfg_write_byte() - 1 byte configuration access write.
 * @uent: UB entity.
 * @pos: Config space address.
 * @val: Data.
 *
 * Initiate configuration access to the specified address of the entity
 * configuration space and write 1 byte.
 *
 * Context: Any context, It will take spin_lock_irqsave()/spin_unlock_restore()
 * Return: 0 if success, or negative value if failed.
 */
int ub_cfg_write_byte(struct ub_entity *uent, u64 pos, u8 val);
int ub_cfg_write_word(struct ub_entity *uent, u64 pos, u16 val);
int ub_cfg_write_dword(struct ub_entity *uent, u64 pos, u32 val);

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

/**
 * ub_cc_supported() - Congestion control capability query.
 * @uent: UB entity.
 *
 * Context: Any context.
 * Return: %true if the entity supports congestion control, %false otherwise.
 */
bool ub_cc_supported(struct ub_entity *uent);

/**
 * ub_cc_enable() - Enable the congestion control capability of the entity.
 * @uent: UB entity.
 *
 * Context: Any context.
 * Return: 0 if success, or %-EINVAL if input parameters are invalid, or
 * %-EPERM if the entity does not support congestion control, or other
 * failed negative values.
 */
int ub_cc_enable(struct ub_entity *uent);

/**
 * ub_cc_disable() - Disable the congestion control capability of the entity.
 * @uent: UB entity.
 *
 * Context: Any context.
 * Return: 0 if success, or %-EINVAL if input parameters are invalid, or
 * %-EPERM if the entity does not support congestion control, or other
 * failed negative values.
 */
int ub_cc_disable(struct ub_entity *uent);

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
static inline bool ub_cc_supported(struct ub_entity *uent)
{ return false; }
static inline int ub_cc_enable(struct ub_entity *uent)
{ return -ENODEV; }
static inline int ub_cc_disable(struct ub_entity *uent)
{ return -ENODEV; }
static inline int ub_cfg_read_byte(struct ub_entity *uent, u64 pos, u8 *val)
{ return -ENODEV; }
static inline int ub_cfg_read_word(struct ub_entity *uent, u64 pos, u16 *val)
{ return -ENODEV; }
static inline int ub_cfg_read_dword(struct ub_entity *uent, u64 pos, u32 *val)
{ return -ENODEV; }
static inline int ub_cfg_write_byte(struct ub_entity *uent, u64 pos, u8 val)
{ return -ENODEV; }
static inline int ub_cfg_write_word(struct ub_entity *uent, u64 pos, u16 val)
{ return -ENODEV; }
static inline int ub_cfg_write_dword(struct ub_entity *uent, u64 pos, u32 val)
{ return -ENODEV; }
static inline int ub_get_bus_controller(struct ub_entity *uents[],
				    unsigned int max_num,
				    unsigned int *real_num)
{ return -ENODEV; }
static inline void
ub_put_bus_controller(struct ub_entity *uents[], unsigned int num) {}
static inline void ub_bus_type_iommu_ops_set(const struct iommu_ops *ops) {}
static inline const struct iommu_ops *ub_bus_type_iommu_ops_get(void)
{ return NULL; }
static inline int ub_register_share_port(struct ub_entity *uent, u16 port_id,
					 struct ub_share_port_ops *ops)
{ return -ENODEV; }
static inline void ub_unregister_share_port(struct ub_entity *uent, u16 port_id,
					    struct ub_share_port_ops *ops) {}
static inline int
__ub_register_driver(struct ub_driver *drv, struct module *owner,
		     const char *mod_name)
{ return 0; }
static inline int ub_register_driver(struct ub_driver *drv)
{ return 0; }
static inline void ub_unregister_driver(struct ub_driver *drv) {}
#endif /* CONFIG_UB_UBUS */

#endif /* _UB_UBUS_UBUS_H_ */

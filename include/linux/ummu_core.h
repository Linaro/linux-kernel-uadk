/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright(c) 2025 HiSilicon Technologies CO., All rights reserved.
 * Description: UMMU-CORE defines and function prototypes.
 */

#ifndef _UMMU_CORE_H_
#define _UMMU_CORE_H_

#include <uapi/linux/ummu_core.h>
#include <linux/iommu.h>
#include <linux/uuid.h>
#include <linux/xarray.h>
#include <linux/property.h>

#define eid_t u128

#define UB_MAX_TID_BITS 20U
#define UB_MAX_TID ((1 << UB_MAX_TID_BITS) - 1)

#define UMMU_NO_TID 0U
#define UMMU_INVALID_TID UB_MAX_TID

#define UMMU_DEV_WRITE 1
#define UMMU_DEV_READ 2
#define UMMU_DEV_ATOMIC 4

enum eid_type {
	EID_NONE = 0,
	EID_BYPASS,
	EID_TYPE_MAX,
};

enum tid_alloc_mode {
	TID_ALLOC_TRANSPARENT = 0,
	TID_ALLOC_ASSIGNED = 1,
	TID_ALLOC_NORMAL = 2,
};

enum ummu_resource_type {
	UMMU_BLOCK,
	UMMU_QUEUE,
	UMMU_QUEUE_LIST,
	UMMU_CNT,
	UMMU_TID_RES,
};

enum default_tid_ops_types {
	PASID_OPS,
	DEFAULT_OPS,
	TID_OPS_MAX,
};

enum ummu_register_type {
	REGISTER_TYPE_GLOBAL,
	REGISTER_TYPE_NORMAL,
	REGISTER_TYPE_MAX,
};

struct iova_slot;
struct ummu_tid_manager;
struct ummu_base_domain;
struct ummu_core_device;

struct block_args {
	u32 index;
	int block_size_order;
	phys_addr_t out_addr;

	KABI_RESERVE(1)
	KABI_RESERVE(2)
	KABI_RESERVE(3)
	KABI_RESERVE(4)
	KABI_RESERVE(5)
	KABI_RESERVE(6)
};

struct queue_args {
	phys_addr_t pcmdq_base;
	phys_addr_t pcplq_base;
	phys_addr_t ctrl_page;

	KABI_RESERVE(1)
	KABI_RESERVE(2)
	KABI_RESERVE(3)
	KABI_RESERVE(4)
	KABI_RESERVE(5)
};

struct tid_args {
	u8 pcmdq_order;
	u8 pcplq_order;
	size_t blk_exp_size;
	u64 hw_cap;

	KABI_RESERVE(1)
	KABI_RESERVE(2)
	KABI_RESERVE(3)
	KABI_RESERVE(4)
	KABI_RESERVE(5)
};

struct resource_args {
	enum ummu_resource_type type;
	union {
		struct block_args block;
		struct queue_args queue;
		struct queue_args *queues;
		struct tid_args tid_res;
		u32 ummu_cnt;
		u32 block_index;
	};
	int align;

	KABI_RESERVE(1)
	KABI_RESERVE(2)
	KABI_RESERVE(3)
};

struct ummu_param {
	enum ummu_mapt_mode mode;

	KABI_RESERVE(1)
	KABI_RESERVE(2)
	KABI_RESERVE(3)
	KABI_RESERVE(4)
	KABI_RESERVE(5)
	KABI_RESERVE(6)
	KABI_RESERVE(7)
};

struct ummu_tid_param {
	struct device *device;
	enum ummu_mapt_mode mode;
	enum tid_alloc_mode alloc_mode;
	u32 assign_tid;
	u32 domain_type;

	KABI_RESERVE(1)
	KABI_RESERVE(2)
	KABI_RESERVE(3)
	KABI_RESERVE(4)
	KABI_RESERVE(5)
};

struct tdev_attr {
	const char *name;
	enum dev_dma_attr dma_attr;
	u8 *priv;
	u32 priv_len;

	KABI_RESERVE(1)
	KABI_RESERVE(2)
	KABI_RESERVE(3)
	KABI_RESERVE(4)
};

/**
 * struct ummu_core_ops - ummu ops for normal use, expand from iommu_ops.
 * @get_resource: Get resource for SVA.
 * @put_resource: Put resource for SVA.
 * @add_eid: Add EID to the UMMU device.
 * @del_eid: Add EID to the UMMU device.
 * @invalidate_cfg: invalid configuration table by tid.
 * @cfg_syn_all: synchronize all configuration table.
 * @cfg_syn: synchronize configuration table by tid.
 * @tdev_support_attr: Check whether the UMMU device supports the tdev attribute.
 */
struct ummu_core_ops {
	int (*get_resource)(struct ummu_base_domain *d, struct resource_args *arg);
	void (*put_resource)(struct ummu_base_domain *d, struct resource_args *arg);
	int (*add_eid)(struct ummu_core_device *dev, guid_t *guid, eid_t eid,
		       enum eid_type type);
	void (*del_eid)(struct ummu_core_device *dev, guid_t *guid, eid_t eid,
			enum eid_type type);
	int (*invalidate_cfg)(struct ummu_base_domain *d);
	void (*cfg_sync_all)(struct ummu_base_domain *d);
	void (*cfg_sync)(struct ummu_base_domain *d);
	bool (*tdev_support_attr)(struct ummu_core_device *dev, struct tdev_attr *attr);

	KABI_RESERVE(1)
	KABI_RESERVE(2)
	KABI_RESERVE(3)
	KABI_RESERVE(4)
	KABI_RESERVE(5)
	KABI_RESERVE(6)
	KABI_RESERVE(7)
	KABI_RESERVE(8)
};

/**
 * ummu-core defined iommu device type
 * @list: used to link all ummu-core devices
 * @tid_manager: tid domain manager.
 * @iommu: iommu prototype
 * @ops: ummu-core defined iommu operations
 */
struct ummu_core_device {
	struct list_head list;
	struct ummu_tid_manager *tid_manager;
	struct iommu_device iommu;
	const struct ummu_core_ops *ops;
	struct device *ummu_core_root;

	KABI_RESERVE(1)
	KABI_RESERVE(2)
	KABI_RESERVE(3)
	KABI_RESERVE(4)
	KABI_RESERVE(5)
	KABI_RESERVE(6)
	KABI_RESERVE(7)
	KABI_RESERVE(8)
};

struct ummu_base_domain {
	struct iommu_domain domain;
	struct ummu_core_device *core_dev;
	struct iommu_domain *parent;
	struct list_head list;
	u32 tid;

	KABI_RESERVE(1)
	KABI_RESERVE(2)
	KABI_RESERVE(3)
	KABI_RESERVE(4)
};
struct tid_ops {
	struct ummu_tid_manager *(*alloc_tid_manager)(
		struct ummu_core_device *core_device, u32 min_tid,
		u32 max_tid);
	void (*free_tid_manager)(struct ummu_tid_manager *manager);
	int (*alloc_tid)(struct ummu_tid_manager *manager,
			 struct ummu_tid_param *param, u32 *tidp);
	void (*free_tid)(struct ummu_tid_manager *manager, u32 tid);

	KABI_RESERVE(1)
	KABI_RESERVE(2)
	KABI_RESERVE(3)
	KABI_RESERVE(4)
};

struct ummu_tid_manager {
	const struct tid_ops *ops;
	struct xarray token_ids;
	u32 min_tid;
	u32 max_tid;
	void *tid_data;

	KABI_RESERVE(1)
	KABI_RESERVE(2)
	KABI_RESERVE(3)
	KABI_RESERVE(4)
};

struct ummu_core_tid_args {
	const struct tid_ops *tid_ops;
	u32 max_tid;
	u32 min_tid;

	KABI_RESERVE(1)
	KABI_RESERVE(2)
	KABI_RESERVE(3)
	KABI_RESERVE(4)
	KABI_RESERVE(5)
	KABI_RESERVE(6)
};

struct ummu_core_init_args {
	const struct ummu_core_ops *core_ops;
	struct ummu_core_tid_args tid_args;
	const struct iommu_ops *iommu_ops;
	struct device *hwdev;

	KABI_RESERVE(1)
	KABI_RESERVE(2)
	KABI_RESERVE(3)
};

/* Memory traffic monitoring of the UB device */
struct ummu_mpam {
#define UMMU_DEV_SET_MPAM	(1 << 0)
#define UMMU_DEV_GET_MPAM	(1 << 1)
#define UMMU_DEV_SET_USER_MPAM_EN	(1 << 2)
#define UMMU_DEV_GET_USER_MPAM_EN	(1 << 3)
	int flags;
	eid_t eid;
	int tid;
	int partid;
	int pmg;
	int s1mpam;
	int user_mpam_en;

	KABI_RESERVE(1)
	KABI_RESERVE(2)
};

enum ummu_device_config_type {
	UMMU_MPAM = 0,
};

#if IS_ENABLED(CONFIG_UB_UMMU_CORE_DRIVER)
extern const struct tid_ops *ummu_core_tid_ops[TID_OPS_MAX];
#else
static const struct tid_ops *ummu_core_tid_ops[TID_OPS_MAX];
#endif /* CONFIG_UB_UMMU_CORE_DRIVER */

static inline struct ummu_core_device *to_ummu_core(struct iommu_device *iommu)
{
	return container_of(iommu, struct ummu_core_device, iommu);
}

static inline struct ummu_base_domain *
to_ummu_base_domain(struct iommu_domain *dom)
{
	return container_of(dom, struct ummu_base_domain, domain);
}

static inline void tdev_attr_init(struct tdev_attr *attr)
{
	attr->dma_attr = DEV_DMA_COHERENT;
	attr->name = NULL;
	attr->priv = NULL;
	attr->priv_len = 0;
}

#ifdef CONFIG_UB_UMMU_CORE
/* EID API */
/**
 * Add a new EID to the UMMU.
 * @guid: entity/device identity.
 * @eid: entity id to be added.
 * @type: eid type.
 *
 * Return: 0 on success, or an error.
 */
int ummu_core_add_eid(guid_t *guid, eid_t eid, enum eid_type type);
/**
 * Delete an EID from the UMMU.
 * @guid: entity/device identity.
 * @eid: entity id to be deleted.
 * @type: eid type.
 */
void ummu_core_del_eid(guid_t *guid, eid_t eid, enum eid_type type);

/* UMMU IOVA API */
/**
 * Allocate a range of IOVA. The input iova size might be aligned.
 * @dev: related device.
 * @size: iova size.
 * @attrs: dma attributes.
 * @iovap: iova address returned here.
 * @sizep: iova size returned here.
 *
 * Return: iova slot which managed the iova range, or an ERR_PTR.
 */
struct iova_slot *dma_alloc_iova(struct device *dev, size_t size,
				 unsigned long attrs, dma_addr_t *iovap,
				 size_t *sizep);

/**
 * Free a range of IOVA.
 * The API is not thread-safe.
 * @slot: iova slot, generated from dma_alloc_iova.
 */
void dma_free_iova(struct iova_slot *slot);

/**
 * Fill a range of IOVA. It allocates pages and maps pages to the iova.
 * The API is not thread-safe.
 * @slot: iova slot, generated from dma_alloc_iova.
 * @iova: iova start.
 * @nr_pages: fill pages count.
 *
 * Return: 0 on success, or an error number.
 */
int ummu_fill_pages(struct iova_slot *slot, dma_addr_t iova, unsigned long nr_pages);

/**
 * Drain a range of IOVA. It unmaps iova and releases pages.
 * The API is not thread-safe.
 * @slot: iova slot, generated from dma_alloc_iova.
 * @iova: iova start.
 * @nr_pages: drain pages count.
 *
 * Return: 0 on success, or an error number.
 */
int ummu_drain_pages(struct iova_slot *slot, dma_addr_t iova, unsigned long nr_pages);
#else
static inline int ummu_core_add_eid(guid_t *guid, eid_t eid, enum eid_type type)
{
	return -EOPNOTSUPP;
}

static inline void ummu_core_del_eid(guid_t *guid, eid_t eid,
				     enum eid_type type)
{
}

static inline struct iova_slot *dma_alloc_iova(struct device *dev, size_t size,
					       unsigned long attrs,
					       dma_addr_t *iovap, size_t *sizep)
{
	return ERR_PTR(-EOPNOTSUPP);
}

static inline void dma_free_iova(struct iova_slot *slot)
{
}
static inline int ummu_fill_pages(struct iova_slot *slot, dma_addr_t iova,
				  unsigned long nr_pages)
{
	return -EOPNOTSUPP;
}

static inline int ummu_drain_pages(struct iova_slot *slot, dma_addr_t iova,
				   unsigned long nr_pages)
{
	return -EOPNOTSUPP;
}
#endif /* CONFIG_UB_UMMU_CORE */

#if IS_ENABLED(CONFIG_UB_UMMU_CORE_DRIVER)
/* UMMU SVA API */
/**
 * Grant va range permission to sva.
 * @sva: related sva handle.
 * @va: va start
 * @size: va size
 * @perm: permission
 * @cookie: struct ummu_token_info*
 *		if (!cookie) {
 *			do not use cookie check.
 *		} else if (cookie->input == 0) {
 *			use this cookie->tokenval
 *		} else if (cookie->input == 1) {
 *			cookie->tokenval = generate new one
 *		} else {
 *			invalid para
 *		}
 *
 * Return: 0 on success, or an error.
 */
int ummu_sva_grant_range(struct iommu_sva *sva, void *va, size_t size, int perm,
			 void *cookie);

/**
 * Ungrant va range permission from sva.
 * @sva: related sva handle.
 * @va: va start
 * @size: va size
 * @cookie: va related cookie,struct ummu_token_info*
 *		if (!cookie) {
 *			do not use cookie check.
 *		} else {
 *			ungrant by cookie->tokenval
 *		}
 *
 * Return: 0 an success, or an error.
 */
int ummu_sva_ungrant_range(struct iommu_sva *sva, void *va, size_t size,
			   void *cookie);

/**
 * Get tid from dev or sva.
 * @dev: related device.
 * @sva: if sva is set, return sva mode related tid; otherwise
 *	 return the dma mode tid.
 * @tidp: tid returned here.
 *
 * Return: 0 on success, or an error.
 */
int ummu_get_tid(struct device *dev, struct iommu_sva *sva, u32 *tidp);

/**
 * Get iommu_domain by tid and dev.
 * @dev: related device.
 * @tid: tid
 *
 * Return: iommu_domain or NULL if failed.
 */
struct iommu_domain *ummu_core_get_domain_by_tid(struct device *dev,
						 u32 tid);

/**
 * Check whether the UMMU works in ksva mode.
 * @domain: related iommu domain
 *
 * Return: true or false.
 */
bool ummu_is_ksva(struct iommu_domain *domain);

/**
 * Check whether the UMMU works in sva mode.
 * @domain: related iommu domain
 *
 * Return: true or false.
 */
bool ummu_is_sva(struct iommu_domain *domain);

/**
 * Bind device to a process mm.
 * @dev: related device.
 * @mm: process memory management.
 * @drvdata: ummu_param related to tid.
 *		if (!drvdata) {
 *			sva is in the bypass mapt mode.
 *		} else {
 *			follow the drvdata->mode to set mapt mode.
 *		}
 *
 * Return: sva handle or NULL if failed.
 */
struct iommu_sva *ummu_sva_bind_device(struct device *dev, struct mm_struct *mm,
				       struct ummu_param *drvdata);

/**
 * Bind device to kernel mm.
 * @dev: related device.
 * @drvdata: ummu_param related to tid. ksva doesn't support bypass mapt.
 *
 * Return: sva handle or NULL if failed.
 */
struct iommu_sva *ummu_ksva_bind_device(struct device *dev,
					struct ummu_param *drvdata);
void ummu_sva_unbind_device(struct iommu_sva *handle);
void ummu_ksva_unbind_device(struct iommu_sva *handle);

/* UMMU CORE API */
/**
 * Initialiase ummu core device.
 * @ummu_core: ummu core device.
 * @args: ummu core init args.
 * UMMU driver should carefully choose the args based on its requirement.
 *	iommu_ops is mandatory.
 *	a. the ummu device need tid allocation capability.
 *		a.1 default tid strategies satisfy the ummu device
 *			-> set tid_ops form ummu_core_tid_ops[TID_OPS_MAX]
 *		a.2 default tid strategies do not satisfy the ummu device
 *			-> implement a new tid_ops in the driver.
 *	b. the ummu device need ummu core ops capability.
 *		-> set core_ops.
 *	c. the ummu device has related hwdev.
 *		-> set hwdev.
 */
int ummu_core_device_init(struct ummu_core_device *ummu_core,
			  struct ummu_core_init_args *args);
/**
 * Deinitialiase ummu core device.
 * @ummu_core: ummu core device.
 */
void ummu_core_device_deinit(struct ummu_core_device *ummu_core);

/**
 * Register ummu core device to the ummu framework.
 * @ummu_core: ummu core device.
 * @type: register type.
	REGISTER_TYPE_GLOBAL: register the ummu device as the global device,
		The ummu device will be the device handle all request.
		e.g. 1. add_eid/del_eid 2. provide ubus iommu ops. etc.

	REGISTER_TYPE_NORMAL: follow the iommu_device register. will not be
		related to the global device. it work as a normal iommu device.
 */
int ummu_core_device_register(struct ummu_core_device *ummu_core,
			      enum ummu_register_type type);
/**
 * Unregister ummu core device from the ummu framework.
 * @dev: the ummu_core device tid belongs to.
 */
void ummu_core_device_unregister(struct ummu_core_device *dev);

/**
 * Invalidate ummu global configuration by tid.
 * @tid: tid
 * Return: 0 on success, or an error.
 */
int ummu_core_invalidate_cfg_table(u32 tid);

/* UMMU TID API */
/**
 * Alloc a tid from ummu framework, and alloc related pasid.
 * @dev: the allocated tid will be attached to.
 * @drvdata: ummu_tid_param related to tid
 * @tidp: the allocated tid returned here.
 *
 * Return: 0 on success, or an error.
 */
int ummu_core_alloc_tid(struct ummu_core_device *dev,
			struct ummu_tid_param *drvdata, u32 *tidp);

/**
 * Free a tid to ummu framework.
 * @dev: the ummu_core device tid belongs to.
 * @tid: token id.
 */
void ummu_core_free_tid(struct ummu_core_device *dev, u32 tid);

/**
 * Get mapt_mode related to the tid.
 * @dev: the ummu_core device tid belongs to.
 * @tid: token id.
 *
 * Return: ummu_mapt_mode, negative for an error.
 */
enum ummu_mapt_mode ummu_core_get_mapt_mode(struct ummu_core_device *dev,
					    u32 tid);

/**
 * Get device related to the tid.
 * It will increase the ref count of the device.
 * @dev: the ummu_core device tid belongs to.
 * @tid: token id.
 *
 * Return: device or NULL if failed.
 */
struct device *ummu_core_get_device(struct ummu_core_device *dev, u32 tid);
void ummu_core_put_device(struct device *dev);

/**
 *  Allocate a virtual device to hold a tid.
 * @attr: attributes of tdev
 * @ptid: tid pointer
 * Return: device on success or NULL error.
 */
struct device *ummu_core_alloc_tdev(struct tdev_attr *attr, u32 *ptid);

/**
 * Free the virtual device
 * @dev: Return value allocated by ummu_core_alloc_tdev
 *
 * Return: 0 on success or an error.
 */
int ummu_core_free_tdev(struct device *dev);

/**
 * Get ummu_tid_type related to the tid.
 * @dev: the ummu_core device tid belongs to.
 * @tid: token id.
 * @tid_type: out param, ummu_tid_type
 *
 * Return: 0 on success , others for an error.
 */
int ummu_core_get_tid_type(struct ummu_core_device *dev, u32 tid,
			   u32 *tid_type);

#else
static inline int ummu_sva_grant_range(struct iommu_sva *sva, void *va,
				       size_t size, int perm, void *cookie)
{
	return -EOPNOTSUPP;
}

static inline int ummu_sva_ungrant_range(struct iommu_sva *sva, void *va,
					 size_t size, void *cookie)
{
	return -EOPNOTSUPP;
}

static inline int ummu_get_tid(struct device *dev, struct iommu_sva *sva,
			       u32 *tidp)
{
	return -EOPNOTSUPP;
}

static inline struct iommu_domain *
ummu_core_get_domain_by_tid(struct device *dev, u32 tid)
{
	return NULL;
}

static inline bool ummu_is_ksva(struct iommu_domain *domain)
{
	return false;
}

static inline bool ummu_is_sva(struct iommu_domain *domain)
{
	return false;
}

static inline struct iommu_sva *ummu_sva_bind_device(struct device *dev,
						     struct mm_struct *mm,
						     struct ummu_param *drvdata)
{
	return NULL;
}

static inline struct iommu_sva *
ummu_ksva_bind_device(struct device *dev, struct ummu_param *drvdata)
{
	return NULL;
}

static inline void ummu_sva_unbind_device(struct iommu_sva *handle)
{
}
static inline void ummu_ksva_unbind_device(struct iommu_sva *handle)
{
}
static inline int ummu_core_device_init(struct ummu_core_device *ummu_core,
					struct ummu_core_init_args *args)
{
	return -EOPNOTSUPP;
}

static inline void ummu_core_device_deinit(struct ummu_core_device *ummu_core)
{
}
static inline int ummu_core_device_register(struct ummu_core_device *ummu_core,
					    enum ummu_register_type type)
{
	return -EOPNOTSUPP;
}

static inline void ummu_core_device_unregister(struct ummu_core_device *dev)
{
}
static inline int ummu_core_invalidate_cfg_table(u32 tid)
{
	return -EOPNOTSUPP;
}

static inline int ummu_core_alloc_tid(struct ummu_core_device *dev,
				      struct ummu_tid_param *drvdata,
				      u32 *tidp)
{
	return -EOPNOTSUPP;
}

static inline void ummu_core_free_tid(struct ummu_core_device *dev,
				      u32 tid)
{
}
static inline enum ummu_mapt_mode
ummu_core_get_mapt_mode(struct ummu_core_device *dev, u32 tid)
{
	return MAPT_MODE_END;
}

static inline struct device *ummu_core_get_device(struct ummu_core_device *dev,
						  u32 tid)
{
	return NULL;
}

static inline void ummu_core_put_device(struct device *dev)
{
}

static inline struct device *ummu_core_alloc_tdev(struct tdev_attr *attr, u32 *ptid)
{
	return NULL;
}

static inline int ummu_core_free_tdev(struct device *dev)
{
	return -EOPNOTSUPP;
}

static inline int ummu_core_get_tid_type(struct ummu_core_device *dev, u32 tid,
					 u32 *tid_type)
{
	return -EOPNOTSUPP;
}
#endif /* CONFIG_UB_UMMU_CORE_DRIVER */
#endif /* _UMMU_CORE_H_ */

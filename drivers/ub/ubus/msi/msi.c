// SPDX-License-Identifier: GPL-2.0+
/*
 * Only support for irqdomain.c.
 */

#include <linux/msi.h>
#include <ub/ubus/ubus.h>
#include <uapi/ub/ubus/ubus_regs.h>
#include <linux/irqdomain.h>
#include <linux/interrupt.h>

struct ub_device_id_manager {
	struct idr device_id_idr;
	spinlock_t lock;
};

static struct ub_device_id_manager ub_entity_idm = {
	.device_id_idr = IDR_INIT(ub_entity_idm.device_id_idr),
	.lock = __SPIN_LOCK_UNLOCKED(ub_entity_idm.lock),
};

int ub_interrupt_id_alloc(struct ub_entity *uent)
{
	int id;

	idr_preload(GFP_KERNEL);
	spin_lock(&ub_entity_idm.lock);
	id = idr_alloc(&ub_entity_idm.device_id_idr, uent,
		       uent->ubc->attr.int_id_start, uent->ubc->attr.int_id_end,
		       GFP_NOWAIT);
	spin_unlock(&ub_entity_idm.lock);
	idr_preload_end();

	if (id < 0)
		return id;
	uent->intr_device_id = id;

	return 0;
}

void ub_interrupt_id_free(struct ub_entity *uent)
{
	spin_lock(&ub_entity_idm.lock);
	idr_remove(&ub_entity_idm.device_id_idr, uent->intr_device_id);
	spin_unlock(&ub_entity_idm.lock);
}
EXPORT_SYMBOL_GPL(ub_interrupt_id_free);

struct ub_entity *msi_desc_to_ub_entity(struct msi_desc *desc)
{
	return to_ub_entity(desc->dev);
}

static void __iomem *ub_vector_desc_base_addr(struct msi_desc *desc)
{
	return desc->ub_intr.vector_base;
}

void __iomem *ub_vector_desc_addr(struct msi_desc *desc)
{
	return desc->ub_intr.vector_base + (desc->ub_intr.intr_attrib.entry_nr *
					    UB_INTR_VECTOR_ENTRY_SIZE);
}

static void __iomem *ub_addr_desc_base_addr(struct msi_desc *desc)
{
	return desc->ub_intr.addr_base;
}

static void __iomem *ub_addr_desc_addr(struct msi_desc *desc)
{
	return desc->ub_intr.addr_base + (desc->ub_intr.intr_attrib.addr_index *
					  UB_INTR_ADDR_ENTRY_SIZE);
}

static void ub_int_type1_update_mask(struct msi_desc *desc, u32 clear, u32 set)
{
	raw_spinlock_t *lock = &msi_desc_to_ub_entity(desc)->usi_lock;
	unsigned long flags;

	raw_spin_lock_irqsave(lock, flags);
	desc->ub_intr.intr_attrib.mask &= ~clear;
	desc->ub_intr.intr_attrib.mask |= set;
	ub_cfg_write_dword(msi_desc_to_ub_entity(desc), UB_INT_TYPE1_INT_MASK,
			   desc->ub_intr.intr_attrib.mask);
	raw_spin_unlock_irqrestore(lock, flags);
}

static void ub_int_type1_mask(struct msi_desc *desc, u32 mask)
{
	ub_int_type1_update_mask(desc, 0, mask);
}

static void ub_int_type2_mask(struct msi_desc *desc)
{
	void __iomem *addr = ub_vector_desc_addr(desc);
	u32 reg_val = readl(addr + UB_INTR_VECTOR_ADDR_INDEX) | UB_INTR_VECTOR_MASK_MASK;

	desc->ub_intr.intr_attrib.mask = 1;
	writel(reg_val, addr + UB_INTR_VECTOR_ADDR_INDEX);
}

static void ub_usi_desc_mask(struct msi_desc *desc, u32 nr)
{
	if (desc->ub_intr.intr_attrib.is_type1)
		ub_int_type1_mask(desc, nr);
	else
		ub_int_type2_mask(desc);
}

void ub_msi_mask_irq(struct irq_data *data)
{
	struct msi_desc *desc = irq_data_get_msi_desc(data);

	ub_usi_desc_mask(desc, (u32)BIT(data->irq - desc->irq));
}

static void ub_int_type1_unmask_irq(struct msi_desc *desc, u32 mask)
{
	ub_int_type1_update_mask(desc, mask, 0);
}

static void ub_int_type2_unmask_irq(struct msi_desc *desc)
{
	void __iomem *addr = ub_vector_desc_addr(desc);
	u32 reg_val = readl(addr + UB_INTR_VECTOR_ADDR_INDEX) & (~UB_INTR_VECTOR_MASK_MASK);

	desc->ub_intr.intr_attrib.mask = 0;
	writel(reg_val, addr + UB_INTR_VECTOR_ADDR_INDEX);
}

static void ub_usi_desc_unmask(struct msi_desc *desc, u32 nr)
{
	if (desc->ub_intr.intr_attrib.is_type1)
		ub_int_type1_unmask_irq(desc, nr);
	else
		ub_int_type2_unmask_irq(desc);
}

void ub_msi_unmask_irq(struct irq_data *data)
{
	struct msi_desc *desc = irq_data_get_msi_desc(data);

	ub_usi_desc_unmask(desc, (u32)BIT(data->irq - desc->irq));
}

void ub_write_interruptid(struct ub_entity *uent)
{
	if (!uent->intr_type1)
		ub_cfg_write_dword(uent, UB_INT_ID, uent->intr_device_id);
	else
		ub_cfg_write_dword(uent, UB_INT_TYPE1_INT_ID, uent->intr_device_id);
}

void __ub_write_msi_msg(struct msi_desc *entry, struct msi_msg *msg) {}

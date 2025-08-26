// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) HiSilicon Technologies Co., Ltd. 2025. All rights reserved.
 */

#define pr_fmt(fmt)	"ubus decoder: " fmt

#include <linux/dma-mapping.h>
#include <linux/ktime.h>
#include <linux/resource_ext.h>

#include "ubus.h"
#include "ubus_controller.h"
#include "omm.h"
#include "decoder.h"

#define MMIO_SIZE_MASK		GENMASK_ULL(18, 16)
#define CMDQ_SIZE_MASK		GENMASK_ULL(15, 12)
#define MMIO_SIZE_OFFSET	16
#define CMDQ_SIZE_OFFSET	12

#define CMDQ_SIZE_USE_MASK	GENMASK(11, 8)
#define CMDQ_SIZE_USE_OFFSET	8
#define CMDQ_ENABLE		0x1
#define CMD_ENTRY_SIZE		16

#define DECODER_PAGE_TABLE_ENTRY_SIZE 8

#define DECODER_QUEUE_TIMEOUT_US 1000000 /* 1s */

static void ub_decoder_uninit_queue(struct ub_decoder *decoder)
{
	iounmap(decoder->cmdq.qbase);
}

static int ub_decoder_init_queue(struct ub_bus_controller *ubc,
				 struct ub_decoder *decoder)
{
	struct ub_entity *uent = ubc->uent;

	if (ubc->ops->register_decoder_base_addr) {
		ubc->ops->register_decoder_base_addr(ubc, &decoder->cmdq.base,
						     &decoder->evtq.base);
	} else {
		ub_err(uent,
		       "ub_bus_controller_ops does not provide register_decoder_base_addr func, exit\n");
		return -EINVAL;
	}

	if (decoder->cmdq.qs == 0) {
		ub_err(uent, "decoder cmdq qs is 0\n");
		return -EINVAL;
	}

	decoder->cmdq.qbase = ioremap(decoder->cmdq.base,
				      (1 << decoder->cmdq.qs) * CMD_ENTRY_SIZE);
	if (!decoder->cmdq.qbase)
		return -ENOMEM;

	return 0;
}

static u32 set_mmio_base_reg(struct ub_decoder *decoder)
{
	u32 ret;

	ret = (u32)ub_cfg_write_dword(decoder->uent, DECODER_MMIO_BA0,
				      lower_32_bits(decoder->mmio_base_addr));
	ret |= (u32)ub_cfg_write_dword(decoder->uent, DECODER_MMIO_BA1,
				       upper_32_bits(decoder->mmio_base_addr));
	if (ret)
		ub_err(decoder->uent, "set decoder mmio base failed\n");

	return ret;
}

static u32 set_page_table_reg(struct ub_decoder *decoder)
{
	u32 ret;

	ret = (u32)ub_cfg_write_dword(decoder->uent, DECODER_MATT_BA0,
				      lower_32_bits(decoder->pgtlb.pgtlb_dma));
	ret |= (u32)ub_cfg_write_dword(decoder->uent, DECODER_MATT_BA1,
				       upper_32_bits(decoder->pgtlb.pgtlb_dma));
	if (ret)
		ub_err(decoder->uent, "set decoder page table reg failed\n");

	return ret;
}

static u32 set_queue_reg(struct ub_decoder *decoder)
{
	u32 val, ret;

	/* init decoder cmdq base addr and pi ci */
	ret = (u32)ub_cfg_write_dword(decoder->uent, DECODER_CMDQ_BASE_ADDR0,
				      lower_32_bits(decoder->cmdq.base));
	ret |= (u32)ub_cfg_write_dword(decoder->uent, DECODER_CMDQ_BASE_ADDR1,
				       upper_32_bits(decoder->cmdq.base));
	ret |= (u32)ub_cfg_write_dword(decoder->uent, DECODER_CMDQ_PROD, 0);
	ret |= (u32)ub_cfg_write_dword(decoder->uent, DECODER_CMDQ_CONS, 0);

	/* set decoder cmdq conf */
	ret |= (u32)ub_cfg_read_dword(decoder->uent, DECODER_CMDQ_CFG, &val);
	decoder->vals.cmdq_cfg_val = val;
	val &= ~CMDQ_SIZE_USE_MASK;
	val |= decoder->cmdq.qs << CMDQ_SIZE_USE_OFFSET;
	val |= CMDQ_ENABLE;
	ret |= (u32)ub_cfg_write_dword(decoder->uent, DECODER_CMDQ_CFG, val);

	if (ret)
		ub_err(decoder->uent, "set decoder queue failed\n");

	return ret;
}

static u32 set_decoder_enable(struct ub_decoder *decoder)
{
	u32 ret = (u32)ub_cfg_write_dword(decoder->uent, DECODER_CTRL, 1);

	if (ret)
		ub_err(decoder->uent, "set decoder enable failed\n");

	return ret;
}

static u32 ub_decoder_device_set(struct ub_decoder *decoder)
{
	u32 ret;

	ret = set_mmio_base_reg(decoder);
	ret |= set_page_table_reg(decoder);
	ret |= set_queue_reg(decoder);
	ret |= set_decoder_enable(decoder);

	return ret;
}

static int ub_decoder_create_page_table(struct ub_decoder *decoder)
{
	struct page_table_desc *invalid_desc = &decoder->invalid_desc;
	struct ub_entity *uent = decoder->uent;
	struct page_table *pgtlb;
	void *pgtlb_base;
	size_t size;

	size = DECODER_PAGE_TABLE_ENTRY_SIZE * DECODER_PAGE_TABLE_SIZE;
	pgtlb = &decoder->pgtlb;
	pgtlb_base = dmam_alloc_coherent(decoder->dev, size,
					 &pgtlb->pgtlb_dma, GFP_KERNEL);
	if (!pgtlb_base) {
		ub_err(uent, "allocate ub decoder page table fail\n");
		return -ENOMEM;
	}
	pgtlb->pgtlb_base = pgtlb_base;

	size = sizeof(*pgtlb->desc_base) * DECODER_PAGE_TABLE_SIZE;
	pgtlb->desc_base = kzalloc(size, GFP_KERNEL);
	if (!pgtlb->desc_base) {
		ub_err(uent, "allocate ub decoder page table desc fail\n");
		goto release_pgtlb;
	}

	invalid_desc->page_base = dmam_alloc_coherent(decoder->dev,
						      RANGE_TABLE_PAGE_SIZE,
						      &invalid_desc->page_dma,
						      GFP_KERNEL);
	if (!invalid_desc->page_base) {
		ub_err(uent, "decoder alloc free page fail\n");
		goto release_desc;
	}
	decoder->invalid_page_dma = (invalid_desc->page_dma &
				     DECODER_PGTBL_PGPRT_MASK) >>
				     DECODER_DMA_PAGE_ADDR_OFFSET;

	ub_decoder_init_page_table(decoder, pgtlb_base);

	return 0;

release_desc:
	kfree(pgtlb->desc_base);
	pgtlb->desc_base = NULL;
release_pgtlb:
	size = DECODER_PAGE_TABLE_ENTRY_SIZE * DECODER_PAGE_TABLE_SIZE;
	dmam_free_coherent(decoder->dev, size, pgtlb_base, pgtlb->pgtlb_dma);
	return -ENOMEM;
}

static void ub_decoder_free_page_table(struct ub_decoder *decoder)
{
	struct page_table_desc *invalid_desc = &decoder->invalid_desc;
	size_t size;

	dmam_free_coherent(decoder->dev, RANGE_TABLE_PAGE_SIZE,
			   invalid_desc->page_base, invalid_desc->page_dma);
	kfree(decoder->pgtlb.desc_base);

	size = DECODER_PAGE_TABLE_ENTRY_SIZE * DECODER_PAGE_TABLE_SIZE;
	dmam_free_coherent(decoder->dev, size, decoder->pgtlb.pgtlb_base,
			   decoder->pgtlb.pgtlb_dma);
}

static void ub_get_decoder_mmio_base(struct ub_bus_controller *ubc,
				     struct ub_decoder *decoder)
{
	struct resource_entry *entry;

	decoder->mmio_base_addr = -1;
	resource_list_for_each_entry(entry, &ubc->resources) {
		if (entry->res->flags == IORESOURCE_MEM &&
		    strstr(entry->res->name, "UB_BUS_CTL") &&
		    entry->res->start < decoder->mmio_base_addr)
			decoder->mmio_base_addr = entry->res->start;
	}

	ub_info(decoder->uent, "decoder mmio base is %#llx\n",
		decoder->mmio_base_addr);
}

static const char * const mmio_size_desc[] = {
	"128Gbyte", "256Gbyte", "512Gbyte", "1Tbyte",
	"2Tbyte", "4Tbyte", "8Tbyte", "16Tbyte"
};

static int ub_get_decoder_cap(struct ub_decoder *decoder)
{
	struct ub_entity *uent = decoder->uent;
	u32 val;
	int ret;

	ret = ub_cfg_read_dword(uent, DECODER_CAP, &val);
	if (ret) {
		ub_err(uent, "read decoder cap failed\n");
		return ret;
	}

	decoder->mmio_size_sup = (val & MMIO_SIZE_MASK) >> MMIO_SIZE_OFFSET;
	decoder->cmdq.qs = (val & CMDQ_SIZE_MASK) >> CMDQ_SIZE_OFFSET;

	return 0;
}

static int ub_create_decoder(struct ub_bus_controller *ubc)
{
	struct ub_entity *uent = ubc->uent;
	struct ub_decoder *decoder;
	int ret;

	decoder = kzalloc(sizeof(*decoder), GFP_KERNEL);
	if (!decoder)
		return -ENOMEM;

	decoder->dev = &ubc->dev;
	decoder->uent = uent;
	mutex_init(&decoder->table_lock);

	ub_get_decoder_mmio_base(ubc, decoder);

	ret = ub_get_decoder_cap(decoder);
	if (ret)
		goto release_decoder;

	ret = ub_decoder_init_queue(ubc, decoder);
	if (ret)
		goto release_decoder;

	ret = ub_decoder_create_page_table(decoder);
	if (ret) {
		ub_err(uent, "decoder create page table failed\n");
		goto release_queue;
	}

	ret = ub_decoder_device_set(decoder);
	if (ret)
		goto release_page_table;

	ubc->decoder = decoder;

	decoder->irq_num = -1;
	decoder->rg_size = SZ_4G;

	ub_info(uent, "decoder create success\n");
	return ret;

release_page_table:
	ub_decoder_free_page_table(decoder);
release_queue:
	ub_decoder_uninit_queue(decoder);
release_decoder:
	kfree(decoder);
	return ret;
}

static void unset_mmio_base_reg(struct ub_decoder *decoder)
{
	struct ub_entity *uent = decoder->uent;
	u32 ret;

	ret = (u32)ub_cfg_write_dword(uent, DECODER_MMIO_BA0, 0);
	ret |= (u32)ub_cfg_write_dword(uent, DECODER_MMIO_BA1, 0);
	if (ret)
		ub_err(uent, "unset mmio base reg failed\n");
}

static void unset_queue_reg(struct ub_decoder *decoder)
{
	struct ub_entity *uent = decoder->uent;
	u32 ret;

	ret = (u32)ub_cfg_write_dword(uent, DECODER_CMDQ_CFG,
				      decoder->vals.cmdq_cfg_val);

	ret |= (u32)ub_cfg_write_dword(uent, DECODER_CMDQ_BASE_ADDR0, 0);
	ret |= (u32)ub_cfg_write_dword(uent, DECODER_CMDQ_BASE_ADDR1, 0);

	if (ret)
		ub_err(uent, "unset queue reg failed\n");
}

static void unset_page_table_reg(struct ub_decoder *decoder)
{
	struct ub_entity *uent = decoder->uent;
	u32 ret;

	ret = (u32)ub_cfg_write_dword(uent, DECODER_MATT_BA0, 0);
	ret |= (u32)ub_cfg_write_dword(uent, DECODER_MATT_BA1, 0);
	if (ret)
		ub_err(uent, "unset page table reg failed\n");
}

static void unset_decoder_enable(struct ub_decoder *decoder)
{
	struct ub_entity *uent = decoder->uent;

	if (ub_cfg_write_dword(uent, DECODER_CTRL, 0))
		ub_err(uent, "unset decoder enable failed\n");
}

static void ub_decoder_device_unset(struct ub_decoder *decoder)
{
	unset_decoder_enable(decoder);
	unset_queue_reg(decoder);
	unset_page_table_reg(decoder);
	unset_mmio_base_reg(decoder);
}

static void ub_remove_decoder(struct ub_bus_controller *ubc)
{
	struct ub_decoder *decoder = ubc->decoder;

	if (!decoder) {
		ub_err(ubc->uent, "remove decoder, decoder is null\n");
		return;
	}

	ub_decoder_device_unset(decoder);

	ub_decoder_free_page_table(decoder);

	ub_decoder_uninit_queue(decoder);

	kfree(decoder);

	ubc->decoder = NULL;
}

struct sync_entry {
	u64 op : 8;
	u64 reserve0 : 4;
	u64 cm : 2;
	u64 ntf_sh : 2;
	u64 ntf_attr : 4;
	u64 reserve1 : 12;
	u64 notify_data : 32;
	u64 reserve2 : 2;
	u64 notify_addr : 50;
	u64 reserve3 : 12;
};

struct tlbi_all_entry {
	u32 op : 8;
	u32 reserve0 : 24;
	u32 reserve1;
	u32 reserve2;
	u32 reserve3;
};

struct tlbi_partial_entry {
	u32 op : 8;
	u32 reserve0 : 24;
	u32 tlbi_addr_base : 28;
	u32 reserve1 : 4;
	u32 tlbi_addr_limt : 28;
	u32 reserve2 : 4;
	u32 reserve3;
};

#define TLBI_ADDR_MASK GENMASK_ULL(43, 20)
#define TLBI_ADDR_OFFSET 20
#define CMDQ_ENT_DWORDS 2

#define NTF_SH_NSH		0b00
#define NTF_SH_OSH		0b10
#define NTF_SH_ISH		0b11
#define NTF_ATTR_IR_NC		0b00
#define NTF_ATTR_IR_WBRA	0b01
#define NTF_ATTR_IR_WT		0b10
#define NTF_ATTR_IR_WB		0b11
#define NTF_ATTR_OR_NC		0b0000
#define NTF_ATTR_OR_WBRA	0b0100
#define NTF_ATTR_OR_WT		0b1000
#define NTF_ATTR_OR_WB		0b1100

#define Q_IDX(qs, p) ((p) & ((1 << (qs)) - 1))
#define Q_WRP(qs, p) ((p) & (1 << (qs)))
#define Q_OVF(p) ((p) & Q_OVERFLOW_FLAG)

enum NOTIFY_TYPE {
	DISABLE_NOTIFY = 0,
	ENABLE_NOTIFY = 1,
};

static bool queue_has_space(struct ub_decoder_queue *q, u32 n)
{
	u32 space, prod, cons;

	prod = Q_IDX(q->qs, q->prod.cmdq_wr_idx);
	cons = Q_IDX(q->qs, q->cons.cmdq_rd_idx);

	if (Q_WRP(q->qs, q->prod.cmdq_wr_idx) ==
	    Q_WRP(q->qs, q->cons.cmdq_rd_idx))
		space = (1 << q->qs) - (prod - cons);
	else
		space = cons - prod;

	return space >= n;
}

static u32 queue_inc_prod_n(struct ub_decoder_queue *q, u32 n)
{
	u32 prod = (Q_WRP(q->qs, q->prod.cmdq_wr_idx) |
		   Q_IDX(q->qs, q->prod.cmdq_wr_idx)) + n;
	return Q_WRP(q->qs, prod) | Q_IDX(q->qs, prod);
}

#define CMD_0_OP		GENMASK_ULL(7, 0)
#define CMD_0_ADDR_BASE		GENMASK_ULL(59, 32)
#define CMD_1_ADDR_LIMT		GENMASK_ULL(27, 0)

static void decoder_cmdq_issue_cmd(struct ub_decoder *decoder, phys_addr_t addr,
				   u64 size, enum ub_cmd_op_type op)
{
	struct ub_decoder_queue *cmdq = &(decoder->cmdq);
	struct tlbi_partial_entry entry = {};
	u64 cmd[CMDQ_ENT_DWORDS] = {};
	void *pi;
	int i;

	entry.op = op;
	entry.tlbi_addr_base = (addr & TLBI_ADDR_MASK) >> TLBI_ADDR_OFFSET;
	entry.tlbi_addr_limt = ((addr + size - 1U) & TLBI_ADDR_MASK) >>
			       TLBI_ADDR_OFFSET;

	cmd[0] |= FIELD_PREP(CMD_0_OP, entry.op);
	cmd[0] |= FIELD_PREP(CMD_0_ADDR_BASE, entry.tlbi_addr_base);
	cmd[1] |= FIELD_PREP(CMD_1_ADDR_LIMT, entry.tlbi_addr_limt);

	pi = cmdq->qbase + Q_IDX(cmdq->qs, cmdq->prod.cmdq_wr_idx) *
	     sizeof(struct tlbi_partial_entry);

	for (i = 0; i < CMDQ_ENT_DWORDS; i++)
		writeq(cmd[i], pi + i * sizeof(u64));

	cmdq->prod.cmdq_wr_idx = queue_inc_prod_n(cmdq, 1);
}

#define NTF_DMA_ADDR_OFFSERT	2
#define SYNC_0_OP		GENMASK_ULL(7, 0)
#define SYNC_0_CM		GENMASK_ULL(13, 12)
#define SYNC_0_NTF_ISH		GENMASK_ULL(15, 14)
#define SYNC_0_NTF_ATTR		GENMASK_ULL(19, 16)
#define SYNC_0_NTF_DATA		GENMASK_ULL(63, 32)
#define SYNC_1_NTF_ADDR		GENMASK_ULL(51, 2)
#define SYNC_NTF_DATA		0xffffffff

static void decoder_cmdq_issue_sync(struct ub_decoder *decoder)
{
	struct ub_decoder_queue *cmdq = &(decoder->cmdq);
	u64 cmd[CMDQ_ENT_DWORDS] = {};
	struct sync_entry entry = {};
	phys_addr_t sync_dma;
	void __iomem *pi;
	int i;

	entry.op = SYNC;
	entry.cm = ENABLE_NOTIFY;
	sync_dma = cmdq->base + Q_IDX(cmdq->qs, cmdq->prod.cmdq_wr_idx) *
		   sizeof(struct sync_entry);
	entry.ntf_sh = NTF_SH_NSH;
	entry.ntf_attr = NTF_ATTR_IR_NC | NTF_ATTR_OR_NC;
	entry.notify_data = SYNC_NTF_DATA;
	entry.notify_addr = sync_dma >> NTF_DMA_ADDR_OFFSERT;

	cmd[0] |= FIELD_PREP(SYNC_0_OP, entry.op);
	cmd[0] |= FIELD_PREP(SYNC_0_CM, entry.cm);
	cmd[0] |= FIELD_PREP(SYNC_0_NTF_ISH, entry.ntf_sh);
	cmd[0] |= FIELD_PREP(SYNC_0_NTF_ATTR, entry.ntf_attr);
	cmd[0] |= FIELD_PREP(SYNC_0_NTF_DATA, entry.notify_data);
	cmd[1] |= FIELD_PREP(SYNC_1_NTF_ADDR, entry.notify_addr);

	pi = cmdq->qbase + Q_IDX(cmdq->qs, cmdq->prod.cmdq_wr_idx) *
	     sizeof(struct sync_entry);
	for (i = 0; i < CMDQ_ENT_DWORDS; i++)
		writeq(cmd[i], pi + i * sizeof(u64));

	decoder->notify = pi;
	cmdq->prod.cmdq_wr_idx = queue_inc_prod_n(cmdq, 1);
}

static void decoder_cmdq_update_prod(struct ub_decoder *decoder)
{
	struct ub_entity *uent = decoder->uent;
	struct queue_idx q;
	int ret;

	ret = ub_cfg_read_dword(uent, DECODER_CMDQ_PROD, &q.val);
	if (ret)
		ub_err(uent, "update pi, read decoder cmdq prod failed\n");

	decoder->cmdq.prod.cmdq_err_resp = q.cmdq_err_resp;
	ret = ub_cfg_write_dword(uent, DECODER_CMDQ_PROD,
				 decoder->cmdq.prod.val);
	if (ret)
		ub_err(uent, "update pi, write decoder cmdq prod failed\n");
}

static int wait_for_cmdq_free(struct ub_decoder *decoder, u32 n)
{
	ktime_t timeout = ktime_add_us(ktime_get(), DECODER_QUEUE_TIMEOUT_US);
	struct ub_decoder_queue *cmdq = &(decoder->cmdq);
	struct ub_entity *uent = decoder->uent;
	int ret;

	while (true) {
		ret = ub_cfg_read_dword(uent, DECODER_CMDQ_CONS,
					&(cmdq->cons.val));
		if (ret)
			return ret;

		if (queue_has_space(cmdq, n + 1))
			return 0;

		if (ktime_compare(ktime_get(), timeout) > 0) {
			ub_err(uent, "decoder cmdq wait free entry timeout\n");
			return -ETIMEDOUT;
		}
		cpu_relax();
	}
}

static int wait_for_cmdq_notify(struct ub_decoder *decoder)
{
	ktime_t timeout;
	u32 val;

	timeout = ktime_add_us(ktime_get(), DECODER_QUEUE_TIMEOUT_US);
	while (true) {
		val = readl(decoder->notify);
		if (val == SYNC_NTF_DATA)
			return 0;

		if (ktime_compare(ktime_get(), timeout) > 0) {
			ub_err(decoder->uent, "decoder cmdq wait notify timeout\n");
			return -ETIMEDOUT;
		}
		cpu_relax();
	}
}

int ub_decoder_cmd_request(struct ub_decoder *decoder, phys_addr_t addr,
			   u64 size, enum ub_cmd_op_type op)
{
	int ret;

	ret = wait_for_cmdq_free(decoder, 1);
	if (ret)
		return ret;

	decoder_cmdq_issue_cmd(decoder, addr, size, op);
	decoder_cmdq_issue_sync(decoder);
	decoder_cmdq_update_prod(decoder);

	ret = wait_for_cmdq_notify(decoder);
	return ret;
}

void ub_decoder_init(struct ub_entity *uent)
{
	int ret;

	if (!uent || !uent->ubc)
		return;

	if (!is_ibus_controller(uent))
		return;

	ret = ub_create_decoder(uent->ubc);
	WARN_ON(ret);
}

void ub_decoder_uninit(struct ub_entity *uent)
{
	if (!uent || !uent->ubc)
		return;

	if (!is_ibus_controller(uent))
		return;

	ub_remove_decoder(uent->ubc);
}

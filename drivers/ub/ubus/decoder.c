// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) HiSilicon Technologies Co., Ltd. 2025. All rights reserved.
 */

#define pr_fmt(fmt)	"ubus decoder: " fmt

#include <linux/dma-mapping.h>
#include <linux/resource_ext.h>

#include "ubus.h"
#include "decoder.h"

#define MMIO_SIZE_MASK GENMASK_ULL(18, 16)
#define MMIO_SIZE_OFFSET 16

static void ub_decoder_uninit_queue(struct ub_decoder *decoder)
{}

static int ub_decoder_init_queue(struct ub_bus_controller *ubc,
				 struct ub_decoder *decoder)
{
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
	return 0;
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
	return 0;
}

static void ub_decoder_free_page_table(struct ub_decoder *decoder)
{}

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
{}

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

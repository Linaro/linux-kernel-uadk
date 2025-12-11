/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) HiSilicon Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef __UBC_H__
#define __UBC_H__

#include "ubrt.h"

#define UBC_VENDOR_INFO_SIZE 256

/**
 * struct ubc_node - Information about a single UBC(UB Controller)
 * @int_id_start:	Start value of the assignable Interrupt ID range
 * @int_id_end:		End value (inclusive) of the range of assignable Interrupt IDs
 * @hpa_base:		Available Host Physical Address Space Base Address
 * @hpa_size:		Size of available Host Physical Address space for allocation
 * @mem_size_limit:	Maximum addressable bit width
 * @dma_cca:		Indicates UBC DMA CCA (Cache Coherent Attribute) capability
 *				0: Supports DMA but does not support CCA
 *				1: Supports both DMA and CCA
 *				Others: Does not support DMA
 * @ummu_mapping:	Indicates the association between this UBC and UMMU,
 *			using the UMMU Index, which is represented by the UMMU
 *			serial number in the UMMU information table.
 * @proximity_domain:	Indicates the NUMA Domain number to which this UBC belongs.
 * @msg_queue_base:	UBC MSG Queue Register Base Address
 * @msg_queue_size:	UBC MSG Queue Register Space Size
 * @msg_queue_depth:	UBC MSG Queue Interrupt Depth
 * @msg_int:		UBC MSG Queue Interrupt Number
 * @msg_int_attr:	UBC MSG Queue Interrupt Attributes
 *			bit0: Level OR Edge Trigger
 *			0: Level
 *			1: Edge
 *			bit1: High level (rising edge) OR Low level (falling edge)
 *			0: High level (rising edge)
 *			1: Low level (falling edge)
 * @vendor_info:	Vendor-defined information
 */
struct ubc_node {
	u32 int_id_start;
	u32 int_id_end;
	u64 hpa_base;
	u64 hpa_size;
	u8 mem_size_limit;
	u8 dma_cca;
	u16 ummu_mapping;
	u16 proximity_domain;
	u16 reserved;
	u64 msg_queue_base;
	u64 msg_queue_size;
	u16 msg_queue_depth;
	u16 msg_int;
	u8 msg_int_attr;
	u8 reserved2[59];
	u64 ubc_guid_low;
	u64 ubc_guid_high;
	u8 vendor_info[UBC_VENDOR_INFO_SIZE];
};

/**
 * struct ubrt_ubc_table - all ubcs in ubrt
 * @header:		Public header of the UBRT table
 * @cna_start:		Starting CNA number available for this UBPU
 * @cna_end:		End CNA number available for this UBPU (inclusive)
 * @eid_start:		Starting EID number available for this UBPU
 * @eid_end:		End EID number available for this UBPU (inclusive)
 * @feature:		Features enabled by UBCs:
 *			bit0: MMIO Token Value, a security feature for MMIO;
 *			0 indicates disabled, 1 indicates enabled.
 *			bit1: MCTP over UB, an MCTP over UB feature;
 *			0 indicates disabled, 1 indicates enabled.
 *			Other bits: reserved.
 * @cluster_mode:	System Operating Mode
 *			0: Standalone mode, the OS can perform UB enumeration.
 *			1: Super Node mode, the OS cannot perform active UB enumeration operations.
 * @ubc_count:		Number of UBCs
 */
struct ubrt_ubc_table {
	struct ub_table_header header;
	u32 cna_start;
	u32 cna_end;
	u32 eid_start;
	u32 eid_end;
	u8 feature;
	u8 reserved[3];
	u16 cluster_mode;
	u16 ubc_count;
	struct ubc_node ubcs[];
};

int handle_ubc_table(u64 pointer);
void destroy_ubc(void);

#endif /* __UBC_H__ */

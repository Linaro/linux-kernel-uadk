/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __ARM64_MM_INTERNAL_H
#define __ARM64_MM_INTERNAL_H

#include <linux/types.h>

#define MAX_RES_REGIONS 32
extern struct memblock_region mbk_memmap_regions[MAX_RES_REGIONS];
extern int mbk_memmap_cnt;

#ifdef CONFIG_PFN_RANGE_ALLOC
#define PFN_RANGE_ALLOC_SIZE PMD_SIZE
#define PFN_RANGE_ALLOC_ORDER PMD_ORDER

static inline bool should_pmd_linear_mapping(void)
{
	return contig_mem_pool_percent == 100;
}

void __init pmd_mapping_reserve_and_remap(void);
void __init pmd_mapping_reserved_remap(phys_addr_t start, phys_addr_t end);
#else
static inline void pmd_mapping_reserve_and_remap(void)
{
}
static inline void pmd_mapping_reserved_remap(phys_addr_t start, phys_addr_t end)
{
}
static inline bool should_pmd_linear_mapping(void)
{
	return false;
}
#endif
#endif /* ifndef _ARM64_MM_INTERNAL_H */

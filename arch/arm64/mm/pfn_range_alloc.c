// SPDX-License-Identifier: GPL-2.0-only
/*
 * Contiguous pfn range allocator
 *
 * Copyright (C) 2025 Huawei Limited.
 */

#define pr_fmt(fmt) "pfn_range_alloc: " fmt

#include <linux/mm.h>
#include <linux/memblock.h>
#include <linux/page-isolation.h>
#include <linux/set_memory.h>
#include "internal.h"

struct pmd_lm_range {
	unsigned long start_pfn;
	unsigned long end_pfn;
};

unsigned long contig_mem_pool_percent __ro_after_init;
EXPORT_SYMBOL_GPL(contig_mem_pool_percent);
static unsigned long nr_reserved_pages[MAX_NUMNODES] __initdata;
static struct pmd_lm_range reserved_range[MAX_NUMNODES];
DEFINE_STATIC_KEY_FALSE(pmd_mapping_initialized);

static inline bool pmd_linear_mapping_enabled(void)
{
	return static_branch_unlikely(&pmd_mapping_initialized);
}

static __init int cmdline_parse_pmd_mapping(char *p)
{
	unsigned long percent;
	char *endptr;

	if (!p)
		return -EINVAL;

	percent = simple_strtoul(p, &endptr, 0);
	if (*endptr != '%' || *(endptr + 1) != '\0')
		return -EINVAL;

	if (percent > 100)
		return -EINVAL;

	contig_mem_pool_percent = percent;

	return 0;
}
early_param("pmd_mapping", cmdline_parse_pmd_mapping);

static __init void calculate_node_nr_reserved_pages(void)
{
	unsigned long start_pfn, end_pfn;
	int i, nid;

	for_each_mem_pfn_range(i, MAX_NUMNODES, &start_pfn, &end_pfn, &nid)
		nr_reserved_pages[nid] += end_pfn - start_pfn;

	for_each_online_node(nid) {
		nr_reserved_pages[nid] = nr_reserved_pages[nid] * contig_mem_pool_percent / 100;
		nr_reserved_pages[nid] = ALIGN_DOWN(nr_reserved_pages[nid],
							PUD_SIZE / PAGE_SIZE);
	}
}

static __init unsigned long calculate_reserve_base(int nid)
{
	pg_data_t *pgdat = NODE_DATA(nid);
	struct zone *zone;
	unsigned long base = 0;

#ifdef CONFIG_ZONE_DMA
	zone = &pgdat->node_zones[ZONE_DMA];
	if (managed_zone(zone))
		base = max(base, PFN_PHYS(zone_end_pfn(zone)));
#endif

#ifdef CONFIG_ZONE_DMA32
	zone = &pgdat->node_zones[ZONE_DMA32];
	if (managed_zone(zone))
		base = max(base, PFN_PHYS(zone_end_pfn(zone)));
#endif

	return base;
}

static __init int __get_suitable_reserved_range(int nid)
{
	unsigned long base, size, start;

	base = calculate_reserve_base(nid);
retry:
	size = nr_reserved_pages[nid] * PAGE_SIZE;
	start = memblock_alloc_range_nid(size, PUD_SIZE, base, 0, nid, true);
	/*
	 * If reservation fails, try to fallback to reserve
	 * smaller size. Fallback is at PUD_SIZE granularity.
	 */
	if (!start) {
		nr_reserved_pages[nid] -= PUD_SIZE / PAGE_SIZE;
		if (!nr_reserved_pages[nid])
			return -ENOMEM;
		goto retry;
	}

	reserved_range[nid].start_pfn = PHYS_PFN(start);
	reserved_range[nid].end_pfn = PHYS_PFN(start) + nr_reserved_pages[nid];

	return 0;
}

static __init int get_suitable_reserved_range(void)
{
	bool restore_bottom_up = false;
	unsigned long start, end;
	bool resved = false;
	int nid, ret;

	if (memblock_bottom_up()) {
		memblock_set_bottom_up(false);
		restore_bottom_up = true;
	}

	calculate_node_nr_reserved_pages();
	for_each_online_node(nid) {
		if (!nr_reserved_pages[nid])
			continue;

		ret = __get_suitable_reserved_range(nid);
		if (ret) {
			pr_warn("reservation failed for node %d\n", nid);
			continue;
		}

		start = PFN_PHYS(reserved_range[nid].start_pfn);
		end = PFN_PHYS(reserved_range[nid].end_pfn);
		pmd_mapping_reserved_remap(start, end);
		resved = true;
		pr_info("reserved %lu MiB on node %d\n", (end - start) / SZ_1M, nid);
	}

	if (restore_bottom_up)
		memblock_set_bottom_up(true);

	return resved;
}

static __init void put_suitable_reserved_range(void)
{
	unsigned long start, end;
	int ret, nid;

	for_each_online_node(nid) {
		start = PFN_PHYS(reserved_range[nid].start_pfn);
		end = PFN_PHYS(reserved_range[nid].end_pfn);

		if (start == end)
			continue;

		ret = memblock_phys_free(start, end - start);
		if (ret)
			pr_warn("put reserved memory [%lx, %lx) failed(%d) for node %d\n",
					start, end, ret, nid);
	}
}

void __init pmd_mapping_reserve_and_remap(void)
{
	bool resved;

	if (!contig_mem_pool_percent)
		return;

	if (should_pmd_linear_mapping())
		goto out;

	if (can_set_direct_map()) {
		pr_info("linear mapping is mapped at PTE level, all memory can be borrowed\n");
		goto out;
	}

	resved = get_suitable_reserved_range();
	if (!resved)
		return;

	put_suitable_reserved_range();
out:
	static_branch_enable(&pmd_mapping_initialized);
}

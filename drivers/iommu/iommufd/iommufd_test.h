/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2021-2022, NVIDIA CORPORATION & AFFILIATES.
 */
#ifndef _UAPI_IOMMUFD_TEST_H
#define _UAPI_IOMMUFD_TEST_H

#include <linux/types.h>
#include <linux/iommufd.h>

enum {
	IOMMU_TEST_OP_ADD_RESERVED = 1,
	IOMMU_TEST_OP_MOCK_DOMAIN,
	IOMMU_TEST_OP_MOCK_DOMAIN_REPLACE,
	IOMMU_TEST_OP_MD_CHECK_MAP,
	IOMMU_TEST_OP_MD_CHECK_REFS,
	IOMMU_TEST_OP_MD_CHECK_IOTLB,
	IOMMU_TEST_OP_CREATE_ACCESS,
	IOMMU_TEST_OP_DESTROY_ACCESS_PAGES,
	IOMMU_TEST_OP_ACCESS_PAGES,
	IOMMU_TEST_OP_ACCESS_RW,
	IOMMU_TEST_OP_SET_TEMP_MEMORY_LIMIT,
};

enum {
	MOCK_APERTURE_START = 1UL << 24,
	MOCK_APERTURE_LAST = (1UL << 31) - 1,
};

enum {
	MOCK_FLAGS_ACCESS_WRITE = 1 << 0,
	MOCK_FLAGS_ACCESS_SYZ = 1 << 16,
};

enum {
	MOCK_ACCESS_RW_WRITE = 1 << 0,
	MOCK_ACCESS_RW_SLOW_PATH = 1 << 2,
};

enum {
	MOCK_FLAGS_ACCESS_CREATE_NEEDS_PIN_PAGES = 1 << 0,
};

struct iommu_test_cmd {
	__u32 size;
	__u32 op;
	__u32 id;
	__u32 __reserved;
	union {
		struct {
			__aligned_u64 start;
			__aligned_u64 length;
		} add_reserved;
		struct {
			__u32 out_device_id;
			__u32 out_hwpt_id;
		} mock_domain;
		struct {
			__u32 device_id;
			__u32 hwpt_id;
		} mock_domain_replace;
		struct {
			__aligned_u64 iova;
			__aligned_u64 length;
			__aligned_u64 uptr;
		} check_map;
		struct {
			__aligned_u64 length;
			__aligned_u64 uptr;
			__u32 refs;
		} check_refs;
		struct {
			__u32 iotlb;
		} check_iotlb;
		struct {
			__u32 out_access_fd;
			__u32 flags;
		} create_access;
		struct {
			__u32 access_pages_id;
		} destroy_access_pages;
		struct {
			__u32 flags;
			__u32 out_access_pages_id;
			__aligned_u64 iova;
			__aligned_u64 length;
			__aligned_u64 uptr;
		} access_pages;
		struct {
			__aligned_u64 iova;
			__aligned_u64 length;
			__aligned_u64 uptr;
			__u32 flags;
		} access_rw;
		struct {
			__u32 limit;
		} memory_limit;
	};
	__u32 last;
};
#define IOMMU_TEST_CMD _IO(IOMMUFD_TYPE, IOMMUFD_CMD_BASE + 32)

/* Mock structs for IOMMU_DEVICE_GET_INFO ioctl */
#define IOMMU_DEVICE_DATA_SELFTEST		0xfeedbeef
#define IOMMU_DEVICE_INFO_SELFTEST_REGVAL	0xdeadbeef

/**
 * struct iommu_device_info_selftest
 *
 * @flags: Must be set to 0
 * @test_reg: Pass IOMMU_DEVICE_INFO_SELFTEST_REGVAL to user selftest program
 */
struct iommu_device_info_selftest {
	__u32 flags;
	__u32 test_reg;
};

/* Should not be equal to any defined value in enum iommu_pgtbl_types */
#define IOMMU_PGTBL_TYPE_SELFTTEST	0xbadbeef

/**
 * struct iommu_hwpt_selftest
 *
 * @flags: page table entry attributes
 * @test_config: default iotlb setup (value IOMMU_TEST_IOTLB_DEFAULT)
 */
struct iommu_hwpt_selftest {
#define IOMMU_TEST_FLAG_NESTED		(1 << 0)
	__u64 flags;
#define IOMMU_TEST_IOTLB_DEFAULT	0xbadbeef
	__u64 test_config;
};

#endif

/* SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note */
/*
 * Copyright (c) HiSilicon Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef _UAPI_UB_UBUS_UBUS_H_
#define _UAPI_UB_UBUS_UBUS_H_

#include <linux/types.h>
#include <linux/ioctl.h>

#define UBUS_API_VERSION 0

/* Kernel & User level defines for UBUS IOCTLs. */

#define UBUS_TYPE ('U')

/* -------- IOCTLs for UBUS file descriptor (/dev/unified_bus) -------- */

/**
 * UBUS_GET_API_VERSION - _IO(UBUS_TYPE, 0)
 *
 * Report the version of the UBUS API.  This allows us to bump the entire
 * API version should we later need to add or change features in incompatible
 * ways.
 * Return: UBUS_API_VERSION
 * Availability: Always
 */
#define UBUS_IOCTL_GET_API_VERSION _IO(UBUS_TYPE, 0)

#define GUID_SIZE 16

struct ubus_cmd_bi_create {
#define UBUS_INSTANCE_DYNAMIC_SERVER 2
#define UBUS_INSTANCE_DYNAMIC_CLUSTER 3
	__u8 type;
	__u16 upi;
	__u32 eid;
	__u8 guid[GUID_SIZE];
};

struct ubus_cmd_bi_destroy {
	__u8 guid[GUID_SIZE];
};

struct ubus_cmd_bi_bind {
	__u8 instance_guid[GUID_SIZE];
	__u8 dev_guid[GUID_SIZE];
};

struct ubus_cmd_bi_unbind {
	__u8 instance_guid[GUID_SIZE];
	__u8 dev_guid[GUID_SIZE];
};

#define UBUS_IOCTL_HEADER_SIZE 8

enum ubus_bi_cmd {
	UBUS_CMD_BI_CREATE = 0,
	UBUS_CMD_BI_DESTROY,
	UBUS_CMD_BI_BIND,
	UBUS_CMD_BI_UNBIND,
	UBUS_CMD_BI_NUM
};

struct ubus_ioctl_bus_instance {
	__u32 argsz; /* size of *union*->structure */
	__u32 sub_cmd;
	union {
		struct ubus_cmd_bi_create create;
		struct ubus_cmd_bi_destroy destroy;
		struct ubus_cmd_bi_bind bind;
		struct ubus_cmd_bi_unbind unbind;
	};
};
#define UBUS_IOCTL_BUS_INSTANCE _IOWR(UBUS_TYPE, 1, struct ubus_ioctl_bus_instance)

#endif /* _UAPI_UB_UBUS_UBUS_H_ */

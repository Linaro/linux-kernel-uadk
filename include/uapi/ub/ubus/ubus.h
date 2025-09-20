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

#endif /* _UAPI_UB_UBUS_UBUS_H_ */

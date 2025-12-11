/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) HiSilicon Technologies Co., Ltd. 2025. All rights reserved.
 */
#ifndef _UB_UBUS_UBUS_IDS_H_
#define _UB_UBUS_UBUS_IDS_H_

/*
 * Device Class base code and sub code
 * +----------+-----------+
 * | sub code | base code |
 * |  [15:8]  |   [7:0]   |
 * +----------+-----------+
 */

#define UB_BASE_CODE_BUS_CONTROLLER	0x00
#define UB_CLASS_BUS_CONTROLLER		0x0000

#define UB_BASE_CODE_STORAGE		0x01
#define UB_CLASS_STORAGE_LPC		0x0001
#define UB_CLASS_STORAGE_LBC		0x0101
#define UB_CLASS_STORAGE_RAID		0x0201

#define UB_BASE_CODE_NETWORK		0x02
#define UB_CLASS_NETWORK_UB		0x0002
#define UB_CLASS_NETWORK_ETH		0x0102

#define UB_BASE_CODE_DISPLAY		0x03

#define UB_BASE_CODE_SWITCH		0x04
#define UB_CLASS_SWITCH_UB		0x0004

#define UB_BASE_CODE_VIRTIO		0x05
#define UB_CLASS_LEGACY_VIRTIO_NETWORK	0x0005
#define UB_CLASS_LEGACY_VIRTIO_BLOCK	0x0105
#define UB_CLASS_LEGACY_VIRTIO_SCSI	0x0205
#define UB_CLASS_LEGACY_VIRTIO_GRAPHIC	0x0305
#define UB_CLASS_LEGACY_VIRTIO_SOCKET	0x0405
#define UB_CLASS_LEGACY_VIRTIO_FS	0x0505

#define UB_BASE_CODE_VIRTUAL		0x06

#define UB_BASE_CODE_NPU		0x07
#define UB_CLASS_NPU_UB			0x0007

#define UB_BASE_CODE_UNKNOWN		0xFF
#define UB_CLASS_UNKNOWN		0x00FF

#endif /* _UB_UBUS_UBUS_IDS_H_ */

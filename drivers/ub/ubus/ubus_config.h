/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) HiSilicon Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef __UBUS_CONFIG_H__
#define __UBUS_CONFIG_H__

struct ub_entity;
int ub_cfg_ops_init(void);
int ub_check_cfg_msg_code(struct device *dev, u8 req_code, u8 rsp_code);
int ub_send_cfg(struct ub_entity *uent, u8 size, u64 pos, u32 *val);

#endif /* __UBUS_CONFIG_H__ */

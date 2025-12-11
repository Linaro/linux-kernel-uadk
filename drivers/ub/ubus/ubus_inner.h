/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) HiSilicon Technologies Co., Ltd. 2025. All rights reserved.
 */
#ifndef __UBUS_INNER_H__
#define __UBUS_INNER_H__

#include <ub/ubus/ubus.h>

typedef int (*read_byte_f)(struct ub_entity *uent, u64 pos, u8 *val);
typedef int (*read_word_f)(struct ub_entity *uent, u64 pos, u16 *val);
typedef int (*read_dword_f)(struct ub_entity *uent, u64 pos, u32 *val);
typedef int (*write_byte_f)(struct ub_entity *uent, u64 pos, u8 val);
typedef int (*write_word_f)(struct ub_entity *uent, u64 pos, u16 val);
typedef int (*write_dword_f)(struct ub_entity *uent, u64 pos, u32 val);

int register_ub_cfg_read_ops(read_byte_f rb, read_word_f rw, read_dword_f rdw);
int register_ub_cfg_write_ops(write_byte_f wb, write_word_f ww, write_dword_f wdw);
void unregister_ub_cfg_ops(void);
struct ub_bus_controller *ub_ubc_get(struct ub_bus_controller *ubc);
void ub_ubc_put(struct ub_bus_controller *ubc);

#endif /* __UBUS_INNER_H__ */

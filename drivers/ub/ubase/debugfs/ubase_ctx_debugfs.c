// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved.
 *
 */

#include <ub/ubase/ubase_comm_debugfs.h>

#include "ubase_debugfs.h"

static void __ubase_print_context_hw(struct seq_file *s, void *ctx_addr,
				     u32 ctx_len)
{
	__le32 *p = (__le32 *)ctx_addr;
	u32 i;

	ctx_len = ctx_len / sizeof(u32);
	for (i = 0; i < ctx_len; i++, p++) {
		seq_printf(s, "%lu\t", (i + 1) * sizeof(u32));
		seq_printf(s, "%08x\n", le32_to_cpu(*p));
	}
}

void ubase_print_context_hw(struct seq_file *s, void *ctx_addr, u32 ctx_len)
{
	if (!s || !ctx_addr)
		return;

	__ubase_print_context_hw(s, ctx_addr, ctx_len);
}
EXPORT_SYMBOL(ubase_print_context_hw);

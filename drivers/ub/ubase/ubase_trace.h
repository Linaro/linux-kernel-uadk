/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved.
 *
 */

/* This must be outside ifdef UBASE_TRACE_H_ */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM ubase

#if !defined(__UBASE_TRACE_H__) || defined(TRACE_HEADER_MULTI_READ)
#define __UBASE_TRACE_H__

#include <linux/tracepoint.h>

#define TRACE_TWO_BYTES_BITS		16
#define TRACE_THREE_BYTES_BITS		24
#define TRACE_CMDQ_DESC_SIZE	(sizeof(struct ubase_cmdq_desc) / sizeof(u32))

#define TRACE_DEV_NAME_MAX_LEN		16

DECLARE_EVENT_CLASS(ubase_cmdq_template,
	TP_PROTO(const struct device *dev, int idx, u32 pi, u32 ci,
		 struct ubase_cmdq_desc *desc),
	TP_ARGS(dev, idx, pi, ci, desc),

	TP_STRUCT__entry(
		__field(int, idx)
		__field(u32, pi)
		__field(u32, ci)
		__array(u32, data, TRACE_CMDQ_DESC_SIZE)
		__dynamic_array(char, devname, TRACE_DEV_NAME_MAX_LEN)
	),

	TP_fast_assign(
		__entry->idx = idx;
		__entry->pi = pi;
		__entry->ci = ci;
		memcpy(&__entry->data, &desc[idx],
		       sizeof(u32) * TRACE_CMDQ_DESC_SIZE);
		if (dev) {
			snprintf(__get_str(devname), TRACE_DEV_NAME_MAX_LEN,
				 "%s %s", dev_driver_string(dev), dev_name(dev));
		}
		ubase_mask_key_words((struct ubase_cmdq_desc *)&__entry->data,
				     desc[0].opcode, idx);
	),

	TP_printk(
		"%s %d-%u-%u data: %s", __get_str(devname),
		__entry->idx, __entry->pi, __entry->ci,
		__print_array(__entry->data, TRACE_CMDQ_DESC_SIZE, sizeof(u32))
	)
);

DEFINE_EVENT(ubase_cmdq_template, ubase_csq_tx,
	TP_PROTO(const struct device *dev, int idx, u32 pi, u32 ci,
		 struct ubase_cmdq_desc *desc),
	TP_ARGS(dev, idx, pi, ci, desc));

DEFINE_EVENT(ubase_cmdq_template, ubase_csq_rx,
	TP_PROTO(const struct device *dev, int idx, u32 pi, u32 ci,
		 struct ubase_cmdq_desc *desc),
	TP_ARGS(dev, idx, pi, ci, desc));

TRACE_EVENT(ubase_crq,
	TP_PROTO(const struct device *dev, int idx, u32 pi, u32 ci,
		 struct ubase_cmdq_desc *desc),
	TP_ARGS(dev, idx, pi, ci, desc),

	TP_STRUCT__entry(
		__field(int, idx)
		__field(u32, pi)
		__field(u32, ci)
		__array(u32, data, TRACE_CMDQ_DESC_SIZE)
		__dynamic_array(char, devname, TRACE_DEV_NAME_MAX_LEN)
	),

	TP_fast_assign(
		__entry->idx = idx;
		__entry->pi = pi;
		__entry->ci = ci;
		memcpy(&__entry->data, desc,
		       sizeof(u32) * TRACE_CMDQ_DESC_SIZE);
		if (dev) {
			snprintf(__get_str(devname), TRACE_DEV_NAME_MAX_LEN,
				 "%s %s", dev_driver_string(dev), dev_name(dev));
		}
	),

	TP_printk(
		"%s %d-%u-%u data: %s", __get_str(devname),
		__entry->idx, __entry->pi, __entry->ci,
		__print_array(__entry->data, TRACE_CMDQ_DESC_SIZE, sizeof(u32))
	)
);

#endif /* __UBASE_TRACE_H__ */

/* This must be outside ifdef __UBASE_TRACE_H__ */
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE ubase_trace
#include <trace/define_trace.h>
